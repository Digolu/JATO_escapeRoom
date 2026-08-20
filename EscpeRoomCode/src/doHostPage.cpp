#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

const char* ssid = "GP_wifi";
const char* password = "qwerty123";

IPAddress local_IP(192, 168, 4, 22);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

const int NUM_ENIGMAS = 1;

const String STR_CORRETO = "CORRETO";
const String STR_ERRADO  = "ERRADO";

bool resolvido[NUM_ENIGMAS];
int tentativas[NUM_ENIGMAS];
String atual[NUM_ENIGMAS];

unsigned long ultimaRecepcao[NUM_ENIGMAS];
const unsigned long TIMEOUT_ONLINE = 5000;

// ---- Estado da equipa atual ----
String equipaAtual = "";
unsigned long horaChegadaMs = 0;
unsigned long horaInicioMs = 0;
bool equipaEmProgresso = false;

const char* FICHEIRO_EQUIPAS = "/equipas.csv";
const char DELIM = '|'; // usado para separar campos no ficheiro. Nao uses "|" no nome da equipa.

void initCodigos() {
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    resolvido[i] = false;
    tentativas[i] = 0;
    atual[i] = "";
    ultimaRecepcao[i] = 0;
  }
}

// Formata milissegundos decorridos desde o arranque do ESP2 como HH:MM:SS
String formatarTempo(unsigned long ms) {
  unsigned long totalSeg = ms / 1000;
  unsigned long h = totalSeg / 3600;
  unsigned long m = (totalSeg % 3600) / 60;
  unsigned long s = totalSeg % 60;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

String jsonEscape(const String &s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// Remove o delimitador do nome da equipa, para nao partir o CSV
String limparNome(String nome) {
  nome.replace(String(DELIM), " ");
  nome.replace("\n", " ");
  nome.replace("\r", " ");
  return nome;
}

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
    server.send(500, "text/plain", "Erro: equipas.html nao encontrado na LittleFS. Faz upload do ficheiro tambem.");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleValores() {
  String json = "{";
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    bool online = (ultimaRecepcao[i] != 0) &&
                  (millis() - ultimaRecepcao[i] < TIMEOUT_ONLINE);

    json += "\"cod" + String(i + 1) + "\":{";
    json += "\"resolvido\":" + String(resolvido[i] ? "true" : "false") + ",";
    json += "\"tentativas\":" + String(tentativas[i]) + ",";
    json += "\"atual\":\"" + jsonEscape(atual[i]) + "\",";
    json += "\"online\":" + String(online ? "true" : "false");
    json += "}";
    if (i < NUM_ENIGMAS - 1) json += ",";
  }
  json += ",\"equipaAtual\":\"" + jsonEscape(equipaAtual) + "\"";
  json += ",\"emProgresso\":" + String(equipaEmProgresso ? "true" : "false");
  if (equipaEmProgresso) {
    json += ",\"tempoDecorrido\":\"" + formatarTempo(millis() - horaInicioMs) + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// Grava uma linha no ficheiro de equipas (append)
void gravarEquipa(unsigned long horaFimMs) {
  File file = LittleFS.open(FICHEIRO_EQUIPAS, "a");
  if (!file) {
    Serial.println("Erro ao abrir equipas.csv para escrita");
    return;
  }

  unsigned long tempoTotalSeg = (horaFimMs - horaInicioMs) / 1000;
  int tentTotal = 0;
  for (int i = 0; i < NUM_ENIGMAS; i++) tentTotal += tentativas[i];

  String linha = equipaAtual;
  linha += DELIM; linha += formatarTempo(horaChegadaMs);
  linha += DELIM; linha += formatarTempo(horaInicioMs);
  linha += DELIM; linha += formatarTempo(horaFimMs);
  linha += DELIM; linha += String(tempoTotalSeg);
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    linha += DELIM; linha += String(tentativas[i]);
  }
  linha += DELIM; linha += String(tentTotal);

  file.println(linha);
  file.close();
  Serial.println("Equipa gravada: " + linha);
}

// Inicia o registo de uma nova equipa e reset ao estado dos enigmas
void handleEquipaNova() {
  if (!server.hasArg("nome") || server.arg("nome").length() == 0) {
    server.send(400, "text/plain", "Falta o parametro 'nome'");
    return;
  }

  equipaAtual = limparNome(server.arg("nome"));
  horaChegadaMs = millis();
  horaInicioMs = millis();
  equipaEmProgresso = true;

  initCodigos(); // reset resolvido, tentativas, atual (mantem ultimaRecepcao para nao perder o online)

  Serial.println("Nova equipa iniciada: " + equipaAtual);
  server.send(200, "text/plain", "OK - equipa '" + equipaAtual + "' iniciada");
}

// Devolve o historico de equipas em JSON
void handleEquipasJson() {
  String json = "[";
  File file = LittleFS.open(FICHEIRO_EQUIPAS, "r");
  bool primeira = true;

  if (file) {
    while (file.available()) {
      String linha = file.readStringUntil('\n');
      linha.trim();
      if (linha.length() == 0) continue;

      // separar pelos campos usando o DELIM
      // formato: nome|horaChegada|horaInicio|horaFim|tempoTotalSeg|tent_enigma1|...|tent_enigmaN|tentativasTotal
      int totalCampos = 5 + NUM_ENIGMAS + 1;
      String campos[20]; // margem folgada; ajusta se tiveres mais de ~14 enigmas
      int idx = 0;
      int start = 0;
      for (int i = 0; i <= linha.length() && idx < totalCampos; i++) {
        if (i == linha.length() || linha[i] == DELIM) {
          campos[idx++] = linha.substring(start, i);
          start = i + 1;
        }
      }
      if (idx < totalCampos) continue; // linha mal formada (ex: gravada com outro NUM_ENIGMAS), ignora

      if (!primeira) json += ",";
      primeira = false;

      json += "{";
      json += "\"nome\":\"" + jsonEscape(campos[0]) + "\",";
      json += "\"horaChegada\":\"" + campos[1] + "\",";
      json += "\"horaInicio\":\"" + campos[2] + "\",";
      json += "\"horaFim\":\"" + campos[3] + "\",";
      json += "\"tempoTotalSeg\":" + campos[4] + ",";
      json += "\"tentativas\":[";
      for (int i = 0; i < NUM_ENIGMAS; i++) {
        json += campos[5 + i];
        if (i < NUM_ENIGMAS - 1) json += ",";
      }
      json += "],";
      json += "\"tentativasTotal\":" + campos[5 + NUM_ENIGMAS];
      json += "}";
    }
    file.close();
  }

  json += "]";
  server.send(200, "application/json", json);
}

// Apaga o historico todo (util para preparar o evento do zero)
void handleEquipasReset() {
  LittleFS.remove(FICHEIRO_EQUIPAS);
  server.send(200, "text/plain", "OK - historico de equipas apagado");
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

      if (valor == STR_CORRETO) {
        resolvido[i] = true;
        Serial.println(nomeParam + ": RESOLVIDO");
      } else if (valor == STR_ERRADO) {
        tentativas[i]++;
        atual[i] = "";
        Serial.println(nomeParam + ": tentativa errada (total " + String(tentativas[i]) + ")");
      } else {
        if (!resolvido[i]) {
          atual[i] = valor;
        }
      }
    } else if (server.hasArg(nomePing)) {
      ultimaRecepcao[i] = millis();
      recebeuAlgo = true;
    }
  }

  // Se a equipa estiver em progresso e TODOS os enigmas ja estiverem resolvidos,
  // grava a equipa e faz reset automatico para a proxima. Funciona com
  // qualquer NUM_ENIGMAS, sem precisar de mexer aqui.
  bool todosResolvidos = true;
  for (int i = 0; i < NUM_ENIGMAS; i++) {
    if (!resolvido[i]) { todosResolvidos = false; break; }
  }

  if (equipaEmProgresso && todosResolvidos) {
    unsigned long horaFimMs = millis();
    gravarEquipa(horaFimMs);
    equipaEmProgresso = false;
    equipaAtual = "";
    initCodigos(); // reset para a proxima equipa
  }

  if (recebeuAlgo) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Falta pelo menos um parametro cod1, cod2, ... ou ping1, ping2, ...");
  }
}

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin()) {
    Serial.println("Erro ao montar LittleFS");
  }

  initCodigos();

  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nESP2 ligado. IP: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/valores", handleValores);
  server.on("/receber", handleReceber);
  server.on("/equipa/nova", handleEquipaNova);
  server.on("/equipas", handleEquipasPagina);
  server.on("/equipas.json", handleEquipasJson);
  server.on("/equipas/reset", handleEquipasReset);
  server.begin();
}

void loop() {
  server.handleClient();
}