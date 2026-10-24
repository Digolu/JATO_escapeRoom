#include <Arduino.h>
#include <Keypad.h>
#include <WiFi.h>
#include <ESP32Servo.h>

Keypad teclado;  // Pinos de fabrica da biblioteca: linhas GPIO2-5, colunas GPIO6-9

#define MAX_CHARS 20
char textoDigitado[MAX_CHARS + 1] = "";
byte posicao = 0;

// ---- Codigo secreto ----
const char codigo[] = "67A89B0";
const byte TAM_CODIGO = 7;

// --------------------- wifi ---------------------
// O C6 liga-se como estacao (cliente) a rede "Jato" ja existente.
// Nao cria rede propria - o router/AP da Jato e quem atribui o IP.
const char* ssid = "JATO";
const char* pass = "08062021";

// IP fixo do Raspberry Pi (servidor Python/Flask) dentro da rede Jato.
// Faixa de IPs fixos livres na Jato: 10.20.0.2 a 10.20.0.49
// (10.20.0.1 = gateway/router; 10.20.0.50-254 = DHCP dinamico)
const char* server = "10.20.0.2";

WiFiClient client;

// ---- Heartbeat (para o servidor saber que este C6 esta online) ----
unsigned long ultimoPing = 0;
const unsigned long INTERVALO_PING = 2000; // manda "estou vivo" a cada 2s

// ---------------- Servo (tranca) ----------------
#define PINO_SERVO 20       // GPIO10/11 sao reservados internamente no modulo C6-MINI-1 (flash in-package)
const int ANGULO_FECHADO = 0;
const int ANGULO_ABERTO  = 180;
const unsigned long DURACAO_SERVO = 10000; // 10 segundos

Servo servoTranca;
bool servoAtivo = false;
unsigned long servoInicio = 0;

void iniciarServo() {
  Serial.println("[SERVO] write ABERTO chamado em iniciarServo()");
  servoTranca.write(ANGULO_ABERTO);
  servoAtivo = true;
  servoInicio = millis();
  Serial.println("Servo: a abrir (180 graus)");
}

void atualizarServo() {
  if (servoAtivo && (millis() - servoInicio >= DURACAO_SERVO)) {
    Serial.println("[SERVO] write FECHADO chamado em atualizarServo()");
    servoTranca.write(ANGULO_FECHADO);
    servoAtivo = false;
    Serial.println("Servo: a fechar (0 graus)");
  }
}

void sendToWifi(String val){
  if (client.connect(server, 80)) {
    String req = "GET /receber?cod1=" + val + " HTTP/1.1\r\n";
    req += "Host: " + String(server) + "\r\n";
    req += "Connection: close\r\n\r\n";

    client.print(req);
    Serial.println("Enviado: " + val);
    client.stop();
    delay(15);
  } else {
    Serial.println("Falha a ligar ao servidor: " + val);
    delay(15);
  }
}

// Envia so um "ping" (nao mexe no estado do enigma, so diz "estou vivo")
void sendPing(){
  if (client.connect(server, 80)) {
    String req = "GET /receber?ping1=1 HTTP/1.1\r\n";
    req += "Host: " + String(server) + "\r\n";
    req += "Connection: close\r\n\r\n";

    client.print(req);
    client.stop();
    delay(15);
  } else {
    Serial.println("Falha no ping.");
    delay(15);
  }
}

void limparEntrada() {
  posicao = 0;
  textoDigitado[0] = '\0';
}

void verificarCodigo() {
  if (strncmp(textoDigitado, codigo, TAM_CODIGO) == 0) {
    Serial.println("CODIGO CORRETO");
    sendToWifi(String("CORRETO"));
    iniciarServo();
  } else {
    sendToWifi(String("ERRADO"));
    Serial.println("CODIGO ERRADO");
  }

  delay(2000);
  limparEntrada();
}

// ---------------- teclado
void verTeclado(){
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
      sendToWifi(String(textoDigitado));
    } else if (posicao < MAX_CHARS) {
      textoDigitado[posicao] = tecla;
      posicao++;
      textoDigitado[posicao] = '\0';

      sendToWifi(String(textoDigitado));

      if (posicao == TAM_CODIGO) {
        delay(500);
        verificarCodigo();
      }
    }
  }
}

// --------------------- set up
void setup() {
  teclado.begin();

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("A ligar a: ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nLigado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  ultimoPing = millis();

  servoTranca.detach();       // garante que o pino fica livre, mesmo que já estivesse ligado
  servoTranca.setPeriodHertz(50);
  servoTranca.attach(PINO_SERVO, 500, 2500);
  servoTranca.write(ANGULO_FECHADO);
}

void loop() {
  verTeclado();
  atualizarServo();

  // Heartbeat: manda um ping periodico mesmo que ninguem esteja a mexer no teclado
  if (millis() - ultimoPing >= INTERVALO_PING) {
    sendPing();
    ultimoPing = millis();
  }
}
