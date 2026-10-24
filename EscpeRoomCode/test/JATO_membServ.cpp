#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Keypad.h>
#include <ESP32Servo.h>

// --------------------- wifi ---------------------
// Liga-se como estacao a rede "Jato" ja existente (nao cria rede propria).
const char* ssid = "JATO";
const char* password = "08062021";

IPAddress local_IP(10, 20, 21, 2);
IPAddress gateway(10, 20, 21, 1);
IPAddress subnet(255, 255, 255, 0);

// IP fixo do ESP C6 que controla o rele (ver src/c6/main.cpp)
const char* IP_C6 = "10.20.21.3";
const int PORTA_C6 = 80;

WebServer server(80);

// ---------------- Teclado (agora ligado diretamente a este ESP) ----------------
// ATENCAO: a lib ZRX543 usa pinos fixos - linhas GPIO2-5, colunas GPIO6-9.
// Num ESP32 "normal" (WROOM-32), GPIO6-11 estao normalmente reservados para a
// flash SPI interna e podem NAO funcionar como GPIO. Confirma com o pinout
// exato da tua placa antes de ligar o teclado a estes pinos.
Keypad teclado;

#define MAX_CHARS 20
char textoDigitado[MAX_CHARS + 1] = "";
byte posicao = 0;

const char codigoSecreto[] = "1679";
const byte TAM_CODIGO = 4;

// ---------------- Servo (tranca) ----------------
#define PINO_SERVO 13   // escolhe um GPIO livre na tua placa - ajusta se precisares
const int ANGULO_FECHADO = 0;
const int ANGULO_ABERTO  = 180;
const unsigned long DURACAO_SERVO = 10000; // 10 segundos

Servo servoTranca;
bool servoAtivo = false;
unsigned long servoInicio = 0;

// ---------------- Enigmas ----------------
const int NUM_ENIGMAS = 1;

const String STR_CORRETO = "CORRETO";
const String STR_ERRADO  = "ERRADO";

bool resolvido[NUM_ENIGMAS];
int tentativas[NUM_ENIGMAS];
String atual[NUM_ENIGMAS];
long tempoEnigmaSeg[NUM_ENIGMAS];
unsigned long ultimaRecepcao[NUM_ENIGMAS];
const unsigned long TIMEOUT_ONLINE = 5000;

// ---- Estado da equipa atual ----
String equipaIdAtual = "";
String equipaAtual = "";
int membrosAtual = 0;
String dataHoraAtual = "";

unsigned long horaInicioMs = 0;
bool equipaEmProgresso = false;

const char* FICHEIRO_PENDENTES = "/equipas_pendentes.csv";
const char* FICHEIRO_RESULTADOS = "/resultados.csv";

const char DELIM = '|';

// ---------------- Rele (fisicamente no C6, espelhado aqui) ----------------
bool releLigado = false;

void initCodigos() {
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    resolvido[i] = false;
    tentativas[i] = 0;
    atual[i] = "";
    tempoEnigmaSeg[i] = -1;
    ultimaRecepcao[i] = 0;
  }
}

bool todosEnigmasResolvidos() {
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    if (!resolvido[i]) {
      return false;
    }
  }
  return true;
}

String formatarTempoSeg(unsigned long totalSeg) {
  unsigned long h = totalSeg / 3600;
  unsigned long m = (totalSeg % 3600) / 60;
  unsigned long s = totalSeg % 60;

  char buf[10];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

String formatarTempoMs(unsigned long ms) {
  return formatarTempoSeg(ms / 1000);
}

String jsonEscape(const String &s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n' || c == '\r') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

String limparCampo(String texto) {
  texto.replace(String(DELIM), " ");
  texto.replace("\n", " ");
  texto.replace("\r", " ");
  texto.trim();
  return texto;
}

int separarCampos(const String &linha, String campos[], int maxCampos) {
  int idx = 0;
  int start = 0;

  for (int i = 0; i <= linha.length() && idx < maxCampos; i++) {
    if (i == linha.length() || linha[i] == DELIM) {
      campos[idx++] = linha.substring(start, i);
      start = i + 1;
    }
  }

  return idx;
}

String criarIdEquipa() {
  return String(millis()) + String(random(1000, 9999));
}

// ---------------- Paginas estaticas ----------------

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");

  if (!file) {
    server.send(500, "text/plain", "Erro: index.html nao encontrado na LittleFS.");
    return;
  }

  server.streamFile(file, "text/html");
  file.close();
}

void handleEquipasPagina() {
  File file = LittleFS.open("/equipas.html", "r");

  if (!file) {
    server.send(500, "text/plain", "Erro: equipas.html nao encontrado na LittleFS.");
    return;
  }

  server.streamFile(file, "text/html");
  file.close();
}

// ---------------- /valores ----------------

void handleValores() {
  String json = "{";

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    bool online = (ultimaRecepcao[i] != 0) &&
                  (millis() - ultimaRecepcao[i] < TIMEOUT_ONLINE);

    json += "\"cod" + String(i + 1) + "\":{";
    json += "\"resolvido\":" + String(resolvido[i] ? "true" : "false") + ",";
    json += "\"tentativas\":" + String(tentativas[i]) + ",";
    json += "\"atual\":\"" + jsonEscape(atual[i]) + "\",";
    json += "\"online\":" + String(online ? "true" : "false") + ",";
    json += "\"tempoEnigmaSeg\":" + String(tempoEnigmaSeg[i]);
    json += "}";

    if (i < NUM_ENIGMAS - 1) {
      json += ",";
    }
  }

  json += ",\"equipaIdAtual\":\"" + jsonEscape(equipaIdAtual) + "\"";
  json += ",\"equipaAtual\":\"" + jsonEscape(equipaAtual) + "\"";
  json += ",\"membrosAtual\":" + String(membrosAtual);
  json += ",\"dataHoraAtual\":\"" + jsonEscape(dataHoraAtual) + "\"";
  json += ",\"emProgresso\":" + String(equipaEmProgresso ? "true" : "false");
  json += ",\"todosResolvidos\":" + String((equipaEmProgresso && todosEnigmasResolvidos()) ? "true" : "false");
  json += ",\"rele\":" + String(releLigado ? "true" : "false");

  if (equipaEmProgresso) {
    json += ",\"tempoDecorrido\":\"" + formatarTempoMs(millis() - horaInicioMs) + "\"";
  }

  json += "}";

  server.send(200, "application/json", json);
}

// ---------------- Resultados ----------------

void gravarResultado(unsigned long horaFimMs) {
  File file = LittleFS.open(FICHEIRO_RESULTADOS, "a");

  if (!file) {
    Serial.println("Erro ao abrir resultados.csv para escrita");
    return;
  }

  unsigned long tempoTotalSeg = (horaFimMs - horaInicioMs) / 1000;

  String linha = equipaIdAtual;
  linha += DELIM;
  linha += equipaAtual;
  linha += DELIM;
  linha += String(membrosAtual);
  linha += DELIM;
  linha += dataHoraAtual;
  linha += DELIM;
  linha += String(tempoTotalSeg);

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    linha += DELIM;

    if (tempoEnigmaSeg[i] < 0) {
      linha += String(tempoTotalSeg);
    } else {
      linha += String(tempoEnigmaSeg[i]);
    }
  }

  file.println(linha);
  file.close();

  Serial.println("Resultado gravado: " + linha);
}

// ---------------- Equipas pendentes ----------------

void handleEquipaAdicionar() {
  if (!server.hasArg("nome") || server.arg("nome").length() == 0) {
    server.send(400, "text/plain", "Falta o parametro 'nome'");
    return;
  }

  if (!server.hasArg("membros") || server.arg("membros").length() == 0) {
    server.send(400, "text/plain", "Falta o parametro 'membros'");
    return;
  }

  if (!server.hasArg("datahora") || server.arg("datahora").length() == 0) {
    server.send(400, "text/plain", "Falta o parametro 'datahora'");
    return;
  }

  String nome = limparCampo(server.arg("nome"));
  int membros = server.arg("membros").toInt();
  String dataHora = limparCampo(server.arg("datahora"));

  if (membros <= 0) {
    server.send(400, "text/plain", "O numero de membros tem de ser maior que zero");
    return;
  }

  String id = criarIdEquipa();

  File file = LittleFS.open(FICHEIRO_PENDENTES, "a");

  if (!file) {
    server.send(500, "text/plain", "Erro ao abrir ficheiro de equipas pendentes");
    return;
  }

  String linha = id;
  linha += DELIM;
  linha += nome;
  linha += DELIM;
  linha += String(membros);
  linha += DELIM;
  linha += dataHora;

  file.println(linha);
  file.close();

  Serial.println("Equipa adicionada: " + linha);

  server.send(200, "text/plain", "OK - equipa adicionada");
}

bool carregarEquipaPendentePorId(const String &id, String &nome, int &membros, String &dataHora) {
  File file = LittleFS.open(FICHEIRO_PENDENTES, "r");

  if (!file) {
    return false;
  }

  while (file.available()) {
    String linha = file.readStringUntil('\n');
    linha.trim();

    if (linha.length() == 0) {
      continue;
    }

    String campos[4];
    int total = separarCampos(linha, campos, 4);

    if (total < 4) {
      continue;
    }

    if (campos[0] == id) {
      nome = campos[1];
      membros = campos[2].toInt();
      dataHora = campos[3];

      file.close();
      return true;
    }
  }

  file.close();
  return false;
}

bool removerEquipaPendentePorId(const String &id) {
  File original = LittleFS.open(FICHEIRO_PENDENTES, "r");

  if (!original) {
    return false;
  }

  File temp = LittleFS.open("/pendentes_tmp.csv", "w");

  if (!temp) {
    original.close();
    return false;
  }

  bool removida = false;

  while (original.available()) {
    String linha = original.readStringUntil('\n');
    linha.trim();

    if (linha.length() == 0) {
      continue;
    }

    String campos[4];
    int total = separarCampos(linha, campos, 4);

    if (total >= 1 && campos[0] == id) {
      removida = true;
      continue;
    }

    temp.println(linha);
  }

  original.close();
  temp.close();

  LittleFS.remove(FICHEIRO_PENDENTES);
  LittleFS.rename("/pendentes_tmp.csv", FICHEIRO_PENDENTES);

  return removida;
}

void handleEquipaIniciar() {
  if (equipaEmProgresso) {
    server.send(409, "text/plain", "Ja existe uma equipa em progresso");
    return;
  }

  if (!server.hasArg("id") || server.arg("id").length() == 0) {
    server.send(400, "text/plain", "Falta o parametro 'id'");
    return;
  }

  String id = server.arg("id");

  String nome;
  int membros = 0;
  String dataHora;

  if (!carregarEquipaPendentePorId(id, nome, membros, dataHora)) {
    server.send(404, "text/plain", "Equipa nao encontrada");
    return;
  }

  if (!removerEquipaPendentePorId(id)) {
    server.send(500, "text/plain", "Erro ao remover equipa da lista de pendentes");
    return;
  }

  equipaIdAtual = id;
  equipaAtual = nome;
  membrosAtual = membros;
  dataHoraAtual = dataHora;

  horaInicioMs = millis();
  equipaEmProgresso = true;

  initCodigos();

  Serial.println("Equipa iniciada: " + equipaAtual);

  server.send(200, "text/plain", "OK - equipa iniciada");
}

void handleEquipaTerminar() {
  if (!equipaEmProgresso) {
    server.send(409, "text/plain", "Nao ha nenhuma equipa em progresso");
    return;
  }

  unsigned long horaFimMs = millis();

  gravarResultado(horaFimMs);

  equipaEmProgresso = false;
  equipaIdAtual = "";
  equipaAtual = "";
  membrosAtual = 0;
  dataHoraAtual = "";

  initCodigos();

  Serial.println("Equipa terminada manualmente.");

  server.send(200, "text/plain", "OK - equipa terminada");
}

void handlePendentesJson() {
  String json = "[";

  File file = LittleFS.open(FICHEIRO_PENDENTES, "r");
  bool primeira = true;

  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();

      if (linha.length() == 0) {
        continue;
      }

      String campos[4];
      int total = separarCampos(linha, campos, 4);

      if (total < 4) {
        continue;
      }

      if (!primeira) {
        json += ",";
      }

      primeira = false;

      json += "{";
      json += "\"id\":\"" + jsonEscape(campos[0]) + "\",";
      json += "\"nome\":\"" + jsonEscape(campos[1]) + "\",";
      json += "\"membros\":" + String(campos[2].toInt()) + ",";
      json += "\"dataHora\":\"" + jsonEscape(campos[3]) + "\"";
      json += "}";
    }

    file.close();
  }

  json += "]";

  server.send(200, "application/json", json);
}

void handleResultadosJson() {
  String json = "[";

  File file = LittleFS.open(FICHEIRO_RESULTADOS, "r");
  bool primeira = true;

  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();

      if (linha.length() == 0) {
        continue;
      }

      int totalCamposEsperados = 5 + NUM_ENIGMAS;
      String campos[30];

      int total = separarCampos(linha, campos, 30);

      if (total < totalCamposEsperados) {
        continue;
      }

      if (!primeira) {
        json += ",";
      }

      primeira = false;

      json += "{";
      json += "\"id\":\"" + jsonEscape(campos[0]) + "\",";
      json += "\"nome\":\"" + jsonEscape(campos[1]) + "\",";
      json += "\"membros\":" + String(campos[2].toInt()) + ",";
      json += "\"dataHora\":\"" + jsonEscape(campos[3]) + "\",";
      json += "\"tempoTotalSeg\":" + String(campos[4].toInt()) + ",";
      json += "\"temposEnigma\":[";

      for (int i = 0; i < NUM_ENIGMAS; i++) {
        json += String(campos[5 + i].toInt());

        if (i < NUM_ENIGMAS - 1) {
          json += ",";
        }
      }

      json += "]";
      json += "}";
    }

    file.close();
  }

  json += "]";

  server.send(200, "application/json", json);
}

void handleResultadosReset() {
  LittleFS.remove(FICHEIRO_RESULTADOS);
  server.send(200, "text/plain", "OK - resultados apagados");
}

void handlePendentesReset() {
  LittleFS.remove(FICHEIRO_PENDENTES);
  server.send(200, "text/plain", "OK - equipas pendentes apagadas");
}

// ---------------- Processamento de enigmas (local e via HTTP) ----------------

// Aplica um valor recebido a um enigma - usado tanto pelo teclado local
// como pelo endpoint /receber (para futuras estacoes externas).
void processarValorEnigma(int i, const String &valor) {
  ultimaRecepcao[i] = millis();

  if (!equipaEmProgresso) {
    return;
  }

  if (valor == STR_CORRETO) {
    if (!resolvido[i]) {
      resolvido[i] = true;
      tempoEnigmaSeg[i] = (millis() - horaInicioMs) / 1000;
      atual[i] = "Resolvido";

      Serial.println("cod" + String(i + 1) + ": RESOLVIDO em " + String(tempoEnigmaSeg[i]) + " segundos");
    }
  } else if (valor == STR_ERRADO) {
    if (!resolvido[i]) {
      tentativas[i]++;
      atual[i] = "";

      Serial.println("cod" + String(i + 1) + ": tentativa errada, total " + String(tentativas[i]));
    }
  } else {
    if (!resolvido[i]) {
      atual[i] = valor;
    }
  }
}

void handleReceber() {
  bool recebeuAlgo = false;

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    String nomeParam = "cod" + String(i + 1);
    String nomePing  = "ping" + String(i + 1);

    if (server.hasArg(nomeParam)) {
      processarValorEnigma(i, server.arg(nomeParam));
      recebeuAlgo = true;
    } else if (server.hasArg(nomePing)) {
      ultimaRecepcao[i] = millis();
      recebeuAlgo = true;
    }
  }

  if (recebeuAlgo) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Falta pelo menos um parametro cod1, cod2, ... ou ping1, ping2, ...");
  }
}

// ---------------- Teclado + servo (locais) ----------------

void iniciarServo() {
  servoTranca.write(ANGULO_ABERTO);
  servoAtivo = true;
  servoInicio = millis();
  Serial.println("Servo: a abrir (180 graus)");
}

void atualizarServo() {
  if (servoAtivo && (millis() - servoInicio >= DURACAO_SERVO)) {
    servoTranca.write(ANGULO_FECHADO);
    servoAtivo = false;
    Serial.println("Servo: a fechar (0 graus)");
  }
}

void limparEntrada() {
  posicao = 0;
  textoDigitado[0] = '\0';
}

void verificarCodigo() {
  if (strncmp(textoDigitado, codigoSecreto, TAM_CODIGO) == 0) {
    Serial.println("CODIGO CORRETO");
    processarValorEnigma(0, STR_CORRETO);
    iniciarServo();
  } else {
    processarValorEnigma(0, STR_ERRADO);
    Serial.println("CODIGO ERRADO");
  }

  delay(2000);
  limparEntrada();
}

void verTeclado() {
  char tecla = teclado.read();

  if (tecla != NONE) {
    Serial.println(tecla);

    if (tecla == '#') {
      limparEntrada();
    } else if (tecla == '*') {
      if (posicao > 0) {
        posicao--;
        textoDigitado[posicao] = '\0';
      }
      processarValorEnigma(0, String(textoDigitado));
    } else if (posicao < MAX_CHARS) {
      textoDigitado[posicao] = tecla;
      posicao++;
      textoDigitado[posicao] = '\0';

      processarValorEnigma(0, String(textoDigitado));

      if (posicao == TAM_CODIGO) {
        delay(500);
        verificarCodigo();
      }
    }
  }
}

// ---------------- Rele (proxy para o C6) ----------------

void enviarComandoRele(const char* caminho) {
  WiFiClient client;

  if (client.connect(IP_C6, PORTA_C6)) {
    String req = "GET " + String(caminho) + " HTTP/1.1\r\n";
    req += "Host: " + String(IP_C6) + "\r\n";
    req += "Connection: close\r\n\r\n";

    client.print(req);
    client.stop();
  } else {
    Serial.println("Falha a contactar o C6 do rele");
  }
}

void handleReleToggle() {
  releLigado = !releLigado;
  enviarComandoRele("/rele/toggle");
  server.send(200, "application/json", "{\"rele\":" + String(releLigado ? "true" : "false") + "}");
}

void handleReleLigar() {
  releLigado = true;
  enviarComandoRele("/rele/ligar");
  server.send(200, "application/json", "{\"rele\":true}");
}

void handleReleDesligar() {
  releLigado = false;
  enviarComandoRele("/rele/desligar");
  server.send(200, "application/json", "{\"rele\":false}");
}

// Recebe o push do C6 a confirmar o estado real do rele.
// GET /rele/estado?ligado=1  ou  ?ligado=0
void handleReleEstado() {
  if (server.hasArg("ligado")) {
    releLigado = (server.arg("ligado") == "1");
  }

  server.send(200, "text/plain", "OK");
}

// ---------------- Setup / loop ----------------

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin()) {
    Serial.println("Erro ao montar LittleFS");
  }

  randomSeed(micros());
  initCodigos();

  teclado.begin();

  servoTranca.detach();
  servoTranca.setPeriodHertz(50);
  servoTranca.attach(PINO_SERVO, 500, 2500);
  servoTranca.write(ANGULO_FECHADO);

  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  Serial.print("A ligar a: ");
  Serial.println(ssid);

  unsigned long inicioLigacao = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.printf(" status=%d", (int)WiFi.status());

    if (millis() - inicioLigacao > 15000) {
      Serial.println("\nTimeout a ligar - a reiniciar tentativa...");
      WiFi.disconnect(true);
      delay(500);
      WiFi.begin(ssid, password);
      inicioLigacao = millis();
    }
  }

  Serial.println("\nLigado! IP: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/valores", handleValores);
  server.on("/receber", handleReceber);

  server.on("/equipas", handleEquipasPagina);

  server.on("/equipa/adicionar", handleEquipaAdicionar);
  server.on("/equipa/iniciar", handleEquipaIniciar);
  server.on("/equipa/terminar", handleEquipaTerminar);

  server.on("/equipas/pendentes.json", handlePendentesJson);
  server.on("/equipas/resultados.json", handleResultadosJson);

  server.on("/equipas/resultados/reset", handleResultadosReset);
  server.on("/equipas/pendentes/reset", handlePendentesReset);

  server.on("/rele/toggle", handleReleToggle);
  server.on("/rele/ligar", handleReleLigar);
  server.on("/rele/desligar", handleReleDesligar);
  server.on("/rele/estado", handleReleEstado);

  server.begin();
}

void loop() {
  server.handleClient();
  verTeclado();
  atualizarServo();

  // Autoheartbeat: o teclado esta no mesmo dispositivo, por isso
  // consideramo-lo sempre "online" enquanto este ESP estiver a correr.
  ultimaRecepcao[0] = millis();
}