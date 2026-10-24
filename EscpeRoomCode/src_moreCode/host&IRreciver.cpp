#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

// --- CONFIGURAÇÕES WIFI & REDE ---
const char* ssid = "GP_wifi";
const char* password = "qwerty123";

IPAddress local_IP(192, 168, 4, 22);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

// --- CONFIGURAÇÕES DE ENIGMAS ---
// Alterado para 2 enigmas (Enigma 1 = Via HTTP / Enigma 2 = Via Comando IR)
const int NUM_ENIGMAS = 2;

// DEFINE AQUI A COMBINAÇÃO SECRETA PARA O ENIGMA 2 (exemplo: 1 - 2 - 3)
const String CHAVE_CORRETA_ENIGMA_2 = "134679"; 

const String STR_CORRETO = "CORRETO";
const String STR_ERRADO  = "ERRADO";

bool resolvido[NUM_ENIGMAS];
int tentativas[NUM_ENIGMAS];
String atual[NUM_ENIGMAS];

long tempoEnigmaSeg[NUM_ENIGMAS];

unsigned long ultimaRecepcao[NUM_ENIGMAS];
const unsigned long TIMEOUT_ONLINE = 5000;

// ---- ESTADO DA EQUIPA ATUAL ----
String equipaIdAtual = "";
String equipaAtual = "";
int membrosAtual = 0;
String dataHoraAtual = "";

unsigned long horaInicioMs = 0;
bool equipaEmProgresso = false;

const char* FICHEIRO_PENDENTES = "/equipas_pendentes.csv";
const char* FICHEIRO_RESULTADOS = "/resultados.csv";

const char DELIM = '|';

// --- CONFIGURAÇÃO DO RECETOR INFRAVERMELHO (ENIGMA 2) ---
const uint16_t kRecvPin = 2; // Pino do sensor IR no ESP32-C6
IRrecv irrecv(kRecvPin);
decode_results results;

// Tabela de Códigos Hexadecimais das Teclas
const uint64_t CODIGO_1 = 0xFF30CF;
const uint64_t CODIGO_2 = 0xFF18E7;
const uint64_t CODIGO_3 = 0xFF7A85;
const uint64_t CODIGO_4 = 0xFF10EF;
const uint64_t CODIGO_5 = 0xFF38C7;
const uint64_t CODIGO_6 = 0xFF5AA5;
const uint64_t CODIGO_7 = 0xFF42BD;
const uint64_t CODIGO_8 = 0xFF4AB5;
const uint64_t CODIGO_9 = 0xFF52AD;
const uint64_t CODIGO_TECLA_LONGA = 0xFFFFFFFFFFFFFFFF;

// --- FUNÇÕES AUXILIARES ---

void initCodigos() {
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    resolvido[i] = false;
    tentativas[i] = 0;
    atual[i] = "";
    tempoEnigmaSeg[i] = -1;
    ultimaRecepcao[i] = 0;
  }
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

void verificarFimJogo() {
  bool todosResolvidos = true;
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    if (!resolvido[i]) {
      todosResolvidos = false;
      break;
    }
  }

  if (equipaEmProgresso && todosResolvidos) {
    unsigned long horaFimMs = millis();

    // Guardar resultados no CSV
    File file = LittleFS.open(FICHEIRO_RESULTADOS, "a");
    if (file) {
      unsigned long tempoTotalSeg = (horaFimMs - horaInicioMs) / 1000;
      String linha = equipaIdAtual + DELIM + equipaAtual + DELIM + String(membrosAtual) + DELIM + dataHoraAtual + DELIM + String(tempoTotalSeg);

      for (int i = 0; i < NUM_ENIGMAS; i++) {
        linha += DELIM;
        linha += (tempoEnigmaSeg[i] < 0) ? String(tempoTotalSeg) : String(tempoEnigmaSeg[i]);
      }
      file.println(linha);
      file.close();
      Serial.println("Resultado gravado: " + linha);
    }

    equipaEmProgresso = false;
    equipaIdAtual = "";
    equipaAtual = "";
    membrosAtual = 0;
    dataHoraAtual = "";

    initCodigos();
    Serial.println("Equipa terminou todos os enigmas!");
  }
}

// --- ROTAS DO SERVIDOR WEB ---

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

void handleValores() {
  String json = "{";

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    bool online = (ultimaRecepcao[i] != 0) && (millis() - ultimaRecepcao[i] < TIMEOUT_ONLINE);

    json += "\"cod" + String(i + 1) + "\":{";
    json += "\"resolvido\":" + String(resolvido[i] ? "true" : "false") + ",";
    json += "\"tentativas\":" + String(tentativas[i]) + ",";
    json += "\"atual\":\"" + jsonEscape(atual[i]) + "\",";
    json += "\"online\":" + String(online ? "true" : "false") + ",";
    json += "\"tempoEnigmaSeg\":" + String(tempoEnigmaSeg[i]);
    json += "}";

    if (i < NUM_ENIGMAS - 1) json += ",";
  }

  json += ",\"equipaIdAtual\":\"" + jsonEscape(equipaIdAtual) + "\"";
  json += ",\"equipaAtual\":\"" + jsonEscape(equipaAtual) + "\"";
  json += ",\"membrosAtual\":" + String(membrosAtual);
  json += ",\"dataHoraAtual\":\"" + jsonEscape(dataHoraAtual) + "\"";
  json += ",\"emProgresso\":" + String(equipaEmProgresso ? "true" : "false");

  if (equipaEmProgresso) {
    json += ",\"tempoDecorrido\":\"" + formatarTempoMs(millis() - horaInicioMs) + "\"";
  }

  json += "}";
  server.send(200, "application/json", json);
}

void handleEquipaAdicionar() {
  if (!server.hasArg("nome") || !server.hasArg("membros") || !server.hasArg("datahora")) {
    server.send(400, "text/plain", "Faltam parametros");
    return;
  }

  String nome = limparCampo(server.arg("nome"));
  int membros = server.arg("membros").toInt();
  String dataHora = limparCampo(server.arg("datahora"));

  if (membros <= 0) {
    server.send(400, "text/plain", "Membros deve ser > 0");
    return;
  }

  File file = LittleFS.open(FICHEIRO_PENDENTES, "a");
  if (!file) {
    server.send(500, "text/plain", "Erro ao abrir ficheiro");
    return;
  }

  String linha = criarIdEquipa() + DELIM + nome + DELIM + String(membros) + DELIM + dataHora;
  file.println(linha);
  file.close();

  server.send(200, "text/plain", "OK - equipa adicionada");
}

void handleEquipaIniciar() {
  if (equipaEmProgresso) {
    server.send(409, "text/plain", "Ja existe uma equipa em progresso");
    return;
  }

  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Falta o parametro 'id'");
    return;
  }

  String id = server.arg("id");
  File original = LittleFS.open(FICHEIRO_PENDENTES, "r");
  if (!original) {
    server.send(404, "text/plain", "Sem equipas pendentes");
    return;
  }

  File temp = LittleFS.open("/pendentes_tmp.csv", "w");
  bool encontrada = false;

  while (original.available()) {
    String linha = original.readStringUntil('\n');
    linha.trim();
    if (linha.length() == 0) continue;

    String campos[4];
    if (separarCampos(linha, campos, 4) >= 4 && campos[0] == id) {
      equipaIdAtual = campos[0];
      equipaAtual = campos[1];
      membrosAtual = campos[2].toInt();
      dataHoraAtual = campos[3];
      encontrada = true;
      continue;
    }
    temp.println(linha);
  }

  original.close();
  temp.close();

  LittleFS.remove(FICHEIRO_PENDENTES);
  LittleFS.rename("/pendentes_tmp.csv", FICHEIRO_PENDENTES);

  if (!encontrada) {
    server.send(404, "text/plain", "Equipa nao encontrada");
    return;
  }

  horaInicioMs = millis();
  equipaEmProgresso = true;
  initCodigos();

  server.send(200, "text/plain", "OK - equipa iniciada");
}

void handlePendentesJson() {
  String json = "[";
  File file = LittleFS.open(FICHEIRO_PENDENTES, "r");
  bool primeira = true;

  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();
      if (linha.length() == 0) continue;

      String campos[4];
      if (separarCampos(linha, campos, 4) < 4) continue;

      if (!primeira) json += ",";
      primeira = false;

      json += "{\"id\":\"" + jsonEscape(campos[0]) + "\",\"nome\":\"" + jsonEscape(campos[1]) + "\",\"membros\":" + String(campos[2].toInt()) + ",\"dataHora\":\"" + jsonEscape(campos[3]) + "\"}";
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
      if (linha.length() == 0) continue;

      String campos[30];
      int total = separarCampos(linha, campos, 30);
      if (total < 5 + NUM_ENIGMAS) continue;

      if (!primeira) json += ",";
      primeira = false;

      json += "{\"id\":\"" + jsonEscape(campos[0]) + "\",\"nome\":\"" + jsonEscape(campos[1]) + "\",\"membros\":" + String(campos[2].toInt()) + ",\"dataHora\":\"" + jsonEscape(campos[3]) + "\",\"tempoTotalSeg\":" + String(campos[4].toInt()) + ",\"temposEnigma\":[";

      for (int i = 0; i < NUM_ENIGMAS; i++) {
        json += String(campos[5 + i].toInt());
        if (i < NUM_ENIGMAS - 1) json += ",";
      }
      json += "]}";
    }
    file.close();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleReceber() {
  bool recebeuAlgo = false;

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    String nomeParam = "cod" + String(i + 1);
    String nomePing  = "ping" + String(i + 1);

    if (server.hasArg(nomeParam)) {
      String valor = server.arg(nomeParam);
      recebeuAlgo = true;
      ultimaRecepcao[i] = millis();

      if (!equipaEmProgresso) continue;

      if (valor == STR_CORRETO && !resolvido[i]) {
        resolvido[i] = true;
        tempoEnigmaSeg[i] = (millis() - horaInicioMs) / 1000;
        atual[i] = "Resolvido";
      } else if (valor == STR_ERRADO && !resolvido[i]) {
        tentativas[i]++;
        atual[i] = "";
      } else if (!resolvido[i]) {
        atual[i] = valor;
      }
    } else if (server.hasArg(nomePing)) {
      ultimaRecepcao[i] = millis();
      recebeuAlgo = true;
    }
  }

  verificarFimJogo();

  if (recebeuAlgo) server.send(200, "text/plain", "OK");
  else server.send(400, "text/plain", "Parametro invalido");
}

// --- FUNÇÃO DO SENSOR IR (ENIGMA 2) ---

int obtermNumeroTeclado() {
    if (irrecv.decode(&results)) {
        uint64_t valorLido = results.value;
        irrecv.resume();

        if (valorLido == CODIGO_1) return 1;
        if (valorLido == CODIGO_2) return 2;
        if (valorLido == CODIGO_3) return 3;
        if (valorLido == CODIGO_4) return 4;
        if (valorLido == CODIGO_5) return 5;
        if (valorLido == CODIGO_6) return 6;
        if (valorLido == CODIGO_7) return 7;
        if (valorLido == CODIGO_8) return 8;
        if (valorLido == CODIGO_9) return 9;
    }
    return 0;
}

void processarLeituraIR() {
    int num = obtermNumeroTeclado();
    
    // Atualiza a presença do Enigma 2 no site
    ultimaRecepcao[1] = millis();

    if (num > 0 && equipaEmProgresso && !resolvido[1]) {
        atual[1] += String(num); // Adiciona a tecla premida ao valor atual
        Serial.print("Enigma 2 - Sequência atual: ");
        Serial.println(atual[1]);

        // Se a sequência coincidir com a palavra-passe do Enigma 2
        if (atual[1] == CHAVE_CORRETA_ENIGMA_2) {
            resolvido[1] = true;
            tempoEnigmaSeg[1] = (millis() - horaInicioMs) / 1000;
            atual[1] = "Resolvido";
            Serial.println("ENIGMA 2 RESOLVIDO VIA COMANDO IR!");
            verificarFimJogo();
        } 
        // Se a sequência digitada for maior que a chave correta, conta como erro e limpa
        else if (atual[1].length() >= CHAVE_CORRETA_ENIGMA_2.length()) {
            tentativas[1]++;
            atual[1] = "";
            Serial.println("Enigma 2 - Sequência incorreta! A reiniciar...");
        }
    }
}

// --- SETUP & LOOP ---

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin()) {
    Serial.println("Erro ao montar LittleFS");
  }

  randomSeed(micros());
  initCodigos();

  // Inicia o Recetor IR
  irrecv.enableIRIn();

  // Configuração Wi-Fi
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nESP-C6 Ligado. IP: " + WiFi.localIP().toString());

  // Registar Rotas
  server.on("/", handleRoot);
  server.on("/valores", handleValores);
  server.on("/receber", handleReceber);
  server.on("/equipas", handleEquipasPagina);
  server.on("/equipa/adicionar", handleEquipaAdicionar);
  server.on("/equipa/iniciar", handleEquipaIniciar);
  server.on("/equipas/pendentes.json", handlePendentesJson);
  server.on("/equipas/resultados.json", handleResultadosJson);
  server.on("/equipas/resultados/reset", []() { LittleFS.remove(FICHEIRO_RESULTADOS); server.send(200, "text/plain", "OK"); });
  server.on("/equipas/pendentes/reset", []() { LittleFS.remove(FICHEIRO_PENDENTES); server.send(200, "text/plain", "OK"); });

  server.begin();
}

void loop() {
  server.handleClient(); 

  processarLeituraIR(); 
  yield();
}