#include <WiFi.h>
#include <WebServer.h>

// --------------------- wifi ---------------------
// Liga-se como estacao a rede "Jato" ja existente (nao cria rede propria).
const char* ssid = "JATO";
const char* password = "08062021";

IPAddress local_IP(10, 20, 21, 3);
IPAddress gateway(10, 20, 21, 1);
IPAddress subnet(255, 255, 255, 0);

// IP fixo do ESP32 que faz a pagina web (ver src/esp32dev/main.cpp)
const char* IP_SERVIDOR = "10.20.21.2";
const int PORTA_SERVIDOR = 80;

WebServer server(80);

// ---------------- Rele ----------------
#define PINO_RELE 5   // CW-019 - logica ativa-baixo (LOW = ligado)

bool releLigado = false;

void aplicarRele() {
  digitalWrite(PINO_RELE, releLigado ? LOW : HIGH);
}

// Avisa o servidor (ESP32 da pagina) do estado atual do rele.
void enviarEstadoParaServidor() {
  WiFiClient client;

  if (client.connect(IP_SERVIDOR, PORTA_SERVIDOR)) {
    String req = "GET /rele/estado?ligado=" + String(releLigado ? "1" : "0") + " HTTP/1.1\r\n";
    req += "Host: " + String(IP_SERVIDOR) + "\r\n";
    req += "Connection: close\r\n\r\n";

    client.print(req);
    client.stop();
  } else {
    Serial.println("Falha a enviar estado do rele ao servidor");
  }
}

void handleToggle() {
  releLigado = !releLigado;
  aplicarRele();

  Serial.println(releLigado ? "Rele: LIGADO" : "Rele: DESLIGADO");

  enviarEstadoParaServidor();
  server.send(200, "text/plain", releLigado ? "1" : "0");
}

void handleLigar() {
  releLigado = true;
  aplicarRele();

  Serial.println("Rele: LIGADO");

  enviarEstadoParaServidor();
  server.send(200, "text/plain", "1");
}

void handleDesligar() {
  releLigado = false;
  aplicarRele();

  Serial.println("Rele: DESLIGADO");

  enviarEstadoParaServidor();
  server.send(200, "text/plain", "0");
}

void handleEstado() {
  server.send(200, "text/plain", releLigado ? "1" : "0");
}

void setup() {
  Serial.begin(115200);

  pinMode(PINO_RELE, OUTPUT);
  releLigado = false;
  aplicarRele(); // comeca desligado

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

  server.on("/rele/toggle", handleToggle);
  server.on("/rele/ligar", handleLigar);
  server.on("/rele/desligar", handleDesligar);
  server.on("/rele/estado", handleEstado);

  server.begin();

  // Informa logo o servidor do estado inicial (desligado).
  enviarEstadoParaServidor();
}

void loop() {
  server.handleClient();
}