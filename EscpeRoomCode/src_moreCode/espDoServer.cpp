#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

const char* ssid = "GP_wifi";
const char* password = "qwerty123";

// Este ESP e agora o proprio ponto de acesso (Access Point).
// O gateway costuma ser igual ao IP do AP.
IPAddress local_IP(192, 168, 4, 22);
IPAddress gateway(192, 168, 4, 22);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

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

  if (equipaEmProgresso) {
    json += ",\"tempoDecorrido\":\"" + formatarTempoMs(millis() - horaInicioMs) + "\"";
  }

  json += "}";

  server.send(200, "application/json", json);
}

// Guarda resultado final.
// Formato:
// id|nome|membros|dataHora|tempoTotalSeg|tempoE1|tempoE2|...
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

// Adiciona equipa à lista de pendentes.
// Parametros:
// nome
// membros
// datahora
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

// Escolhe uma equipa pendente e inicia os enigmas.
// Parametro:
// id
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

// Termina manualmente a equipa em progresso: grava o resultado e liberta
// o sistema para a proxima equipa. So o operador decide quando isto acontece
// (nao acontece sozinho quando o enigma e resolvido).
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

void handleReceber() {
  bool recebeuAlgo = false;

  for (int i = 0; i < NUM_ENIGMAS; i++) {
    String nomeParam = "cod" + String(i + 1);
    String nomePing  = "ping" + String(i + 1);

    if (server.hasArg(nomeParam)) {
      String valor = server.arg(nomeParam);
      recebeuAlgo = true;
      ultimaRecepcao[i] = millis();

      // Se nao houver equipa em progresso, apenas atualiza o estado online.
      if (!equipaEmProgresso) {
        continue;
      }

      if (valor == STR_CORRETO) {
        if (!resolvido[i]) {
          resolvido[i] = true;
          tempoEnigmaSeg[i] = (millis() - horaInicioMs) / 1000;
          atual[i] = "Resolvido";

          Serial.println(nomeParam + ": RESOLVIDO em " + String(tempoEnigmaSeg[i]) + " segundos");
        }
      } else if (valor == STR_ERRADO) {
        if (!resolvido[i]) {
          tentativas[i]++;
          atual[i] = "";

          Serial.println(nomeParam + ": tentativa errada, total " + String(tentativas[i]));
        }
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

  // Nota: ja nao terminamos a equipa automaticamente aqui quando todos os
  // enigmas ficam resolvidos. O jogo so termina quando o operador chamar
  // explicitamente /equipa/terminar (ver botao "Terminar equipa" no site).

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

  randomSeed(micros());

  initCodigos();

  // Este ESP passa a ser o proprio Access Point: cria a rede GP_wifi
  // em vez de se ligar a uma rede existente.
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  Serial.println("\nAP GP_wifi criado. IP: " + WiFi.softAPIP().toString());

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

  server.begin();
}

void loop() {
  server.handleClient();
}
