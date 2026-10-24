#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Keypad.h>
#include <ESP32Servo.h>

// --------------------- wifi ---------------------
const char* ssid = "JATO";
const char* password = "08062021";

IPAddress local_IP(10, 20, 21, 2);
IPAddress gateway(10, 20, 21, 1);
IPAddress subnet(255, 255, 255, 0);

// IP fixo do ESP C6 que controla o rele
const char* IP_C6 = "10.20.21.3";
const int PORTA_C6 = 80;

WebServer server(80);

// ---------------- Teclado ----------------
Keypad teclado;

#define MAX_CHARS 20
char textoDigitado[MAX_CHARS + 1] = "";
byte posicao = 0;

const char codigoSecreto[] = "1679";
const char codigoAdminAbrir[] = "2580";   // Podes alterar o codigo de admin para abrir
const char codigoAdminFechar[] = "0852";  // Podes alterar o codigo de admin para fechar
const byte TAM_CODIGO = 4;

// ---------------- Servo (tranca) ----------------
#define PINO_SERVO 13
const int ANGULO_FECHADO = 100;
const int ANGULO_ABERTO  = 10;

Servo servoTranca;
bool servoAberto = false; // Substituiu o temporizador: agora fica aberto ate receber ordem para fechar

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

// ---------------- Rele ----------------
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
  json += ",\"servoAberto\":" + String(servoAberto ? "true" : "false");

  if (equipaEmProgresso) {
    json += ",\"tempoDecorrido\":\"" + formatarTempoMs(millis() - horaInicioMs) + "\"";
  }

  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- Resultados e Equipas Pendentes (Mantido) ----------------

void gravarResultado(unsigned long horaFimMs) {
  File file = LittleFS.open(FICHEIRO_RESULTADOS, "a");
  if (!file) return;

  unsigned long tempoTotalSeg = (horaFimMs - horaInicioMs) / 1000;
  String linha = equipaIdAtual + DELIM + equipaAtual + DELIM + String(membrosAtual) + DELIM + dataHoraAtual + DELIM + String(tempoTotalSeg);

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    linha += DELIM;
    linha += String(tempoEnigmaSeg[i] < 0 ? tempoTotalSeg : tempoEnigmaSeg[i]);
  }

  file.println(linha);
  file.close();
}

void handleEquipaAdicionar() {
  if (!server.hasArg("nome") || !server.hasArg("membros") || !server.hasArg("datahora")) {
    server.send(400, "text/plain", "Faltam parametros");
    return;
  }
  String nome = limparCampo(server.arg("nome"));
  int membros = server.arg("membros").toInt();
  String dataHora = limparCampo(server.arg("datahora"));
  String id = criarIdEquipa();

  File file = LittleFS.open(FICHEIRO_PENDENTES, "a");
  if (!file) {
    server.send(500, "text/plain", "Erro ao abrir pendentes");
    return;
  }
  file.println(id + DELIM + nome + DELIM + String(membros) + DELIM + dataHora);
  file.close();
  server.send(200, "text/plain", "OK - equipa adicionada");
}

bool carregarEquipaPendentePorId(const String &id, String &nome, int &membros, String &dataHora) {
  File file = LittleFS.open(FICHEIRO_PENDENTES, "r");
  if (!file) return false;
  while (file.available()) {
    String linha = file.readStringUntil('\n');
    linha.trim();
    String campos[4];
    if (separarCampos(linha, campos, 4) >= 4 && campos[0] == id) {
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
  if (!original) return false;
  File temp = LittleFS.open("/pendentes_tmp.csv", "w");
  if (!temp) {
    original.close();
    return false;
  }
  bool removida = false;
  while (original.available()) {
    String linha = original.readStringUntil('\n');
    linha.trim();
    String campos[4];
    if (separarCampos(linha, campos, 4) >= 1 && campos[0] == id) {
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
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Falta o id");
    return;
  }
  String id = server.arg("id");
  String nome; int membros = 0; String dataHora;
  if (!carregarEquipaPendentePorId(id, nome, membros, dataHora) || !removerEquipaPendentePorId(id)) {
    server.send(404, "text/plain", "Equipa nao encontrada");
    return;
  }

  equipaIdAtual = id;
  equipaAtual = nome;
  membrosAtual = membros;
  dataHoraAtual = dataHora;
  horaInicioMs = millis();
  equipaEmProgresso = true;
  initCodigos();
  server.send(200, "text/plain", "OK - equipa iniciada");
}

void handleEquipaTerminar() {
  if (!equipaEmProgresso) {
    server.send(409, "text/plain", "Nao ha equipa em progresso");
    return;
  }
  gravarResultado(millis());
  equipaEmProgresso = false;
  equipaIdAtual = "";
  equipaAtual = "";
  membrosAtual = 0;
  dataHoraAtual = "";
  initCodigos();
  server.send(200, "text/plain", "OK - equipa terminada");
}

void handlePendentesJson() {
  File file = LittleFS.open(FICHEIRO_PENDENTES, "r");
  String json = "[";
  bool primeira = true;
  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();
      String campos[4];
      if (separarCampos(linha, campos, 4) >= 4) {
        if (!primeira) json += ",";
        primeira = false;
        json += "{\"id\":\"" + jsonEscape(campos[0]) + "\",\"nome\":\"" + jsonEscape(campos[1]) + "\",\"membros\":" + campos[2] + ",\"dataHora\":\"" + jsonEscape(campos[3]) + "\"}";
      }
    }
    file.close();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleResultadosJson() {
  File file = LittleFS.open(FICHEIRO_RESULTADOS, "r");
  String json = "[";
  bool primeira = true;
  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();
      String campos[30];
      int totalCamposEsperados = 5 + NUM_ENIGMAS;
      if (separarCampos(linha, campos, 30) >= totalCamposEsperados) {
        if (!primeira) json += ",";
        primeira = false;
        json += "{\"id\":\"" + jsonEscape(campos[0]) + "\",\"nome\":\"" + jsonEscape(campos[1]) + "\",\"membros\":" + campos[2] + ",\"dataHora\":\"" + jsonEscape(campos[3]) + "\",\"tempoTotalSeg\":" + campos[4] + ",\"temposEnigma\":[";
        for (int i = 0; i < NUM_ENIGMAS; i++) {
          json += campos[5 + i];
          if (i < NUM_ENIGMAS - 1) json += ",";
        }
        json += "]}";
      }
    }
    file.close();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleResultadosReset() { LittleFS.remove(FICHEIRO_RESULTADOS); server.send(200, "text/plain", "OK"); }
void handlePendentesReset() { LittleFS.remove(FICHEIRO_PENDENTES); server.send(200, "text/plain", "OK"); }

// ---------------- Gestão do Servo ----------------

void abrirServo() {
  servoTranca.write(ANGULO_ABERTO);
  servoAberto = true;
  Serial.println("Servo: ABERTO (Fica aberto)");
}

void fecharServo() {
  servoTranca.write(ANGULO_FECHADO);
  servoAberto = false;
  Serial.println("Servo: FECHADO");
}

// Endpoint web de admin para abrir/fechar com pedido de código
void handleAdminServo() {
  if (!server.hasArg("codigo")) {
    server.send(400, "text/plain", "Falta o parametro 'codigo'");
    return;
  }

  String codigo = server.arg("codigo");

  if (codigo == codigoAdminAbrir || codigo == codigoSecreto) {
    abrirServo();
    server.send(200, "text/plain", "OK - Servo aberto com sucesso");
  } else if (codigo == codigoAdminFechar) {
    fecharServo();
    server.send(200, "text/plain", "OK - Servo fechado com sucesso");
  } else {
    server.send(403, "text/plain", "Codigo incorreto!");
  }
}

// ---------------- Teclado ----------------

void processarValorEnigma(int i, const String &valor) {
  ultimaRecepcao[i] = millis();
  if (!equipaEmProgresso) return;

  if (valor == STR_CORRETO) {
    if (!resolvido[i]) {
      resolvido[i] = true;
      tempoEnigmaSeg[i] = (millis() - horaInicioMs) / 1000;
      atual[i] = "Resolvido";
    }
  } else if (valor == STR_ERRADO) {
    if (!resolvido[i]) {
      tentativas[i]++;
      atual[i] = "";
    }
  } else {
    if (!resolvido[i]) {
      atual[i] = valor;
    }
  }
}

void limparEntrada() {
  posicao = 0;
  textoDigitado[0] = '\0';
}

void verificarCodigo() {
  if (strncmp(textoDigitado, codigoSecreto, TAM_CODIGO) == 0) {
    Serial.println("CODIGO CORRETO (Enigma)");
    processarValorEnigma(0, STR_CORRETO);
    abrirServo();
  } 
  else if (strncmp(textoDigitado, codigoAdminAbrir, TAM_CODIGO) == 0) {
    Serial.println("CODIGO ADMIN: ABRIR");
    abrirServo();
  } 
  else if (strncmp(textoDigitado, codigoAdminFechar, TAM_CODIGO) == 0) {
    Serial.println("CODIGO ADMIN: FECHAR");
    fecharServo();
  } 
  else {
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

// ---------------- Rele (C6) ----------------
void enviarComandoRele(const char* caminho) {
  WiFiClient client;
  if (client.connect(IP_C6, PORTA_C6)) {
    String req = "GET " + String(caminho) + " HTTP/1.1\r\n";
    req += "Host: " + String(IP_C6) + "\r\nConnection: close\r\n\r\n";
    client.print(req);
    client.stop();
  }
}

void handleReleToggle() { releLigado = !releLigado; enviarComandoRele("/rele/toggle"); server.send(200, "application/json", "{\"rele\":" + String(releLigado ? "true" : "false") + "}"); }
void handleReleLigar() { releLigado = true; enviarComandoRele("/rele/ligar"); server.send(200, "application/json", "{\"rele\":true}"); }
void handleReleDesligar() { releLigado = false; enviarComandoRele("/rele/desligar"); server.send(200, "application/json", "{\"rele\":false}"); }
void handleReleEstado() { if (server.hasArg("ligado")) releLigado = (server.arg("ligado") == "1"); server.send(200, "text/plain", "OK"); }

// ---------------- Setup / Loop ----------------

void setup() {
  Serial.begin(115200);
  if (!LittleFS.begin()) Serial.println("Erro ao montar LittleFS");

  randomSeed(micros());
  initCodigos();
  teclado.begin();

  servoTranca.setPeriodHertz(50);
  servoTranca.attach(PINO_SERVO, 500, 2500);
  servoTranca.write(ANGULO_FECHADO); // Começa fechado

  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // Endpoints web
  server.on("/", handleRoot);
  server.on("/valores", handleValores);
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

  // Endpoint de admin para o servo
  server.on("/admin/servo", handleAdminServo);

  server.begin();
}

void loop() {
  server.handleClient();
  verTeclado();
  // Nota: handleServo removido pois agora ele fica fixo na posição (aberto ou fechado) até receber nova ordem.
  ultimaRecepcao[0] = millis();
}