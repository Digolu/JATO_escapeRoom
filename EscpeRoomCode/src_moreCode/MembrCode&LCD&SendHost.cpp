#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFiEsp.h>

LiquidCrystal_I2C lcd(0x27, 20, 4);
Keypad teclado;

#define MAX_CHARS 20
char textoDigitado[MAX_CHARS + 1] = "";
byte posicao = 0;

// ---- Codigo secreto ----
const char codigo[] = "67A89B0";
const byte TAM_CODIGO = 7;

// --------------------- wifi things
char ssid[] = "GP_wifi";
char pass[] = "qwerty123";
char server[] = "192.168.4.22"; // IP do C6

WiFiEspClient client;
int status = WL_IDLE_STATUS;

// ---- Heartbeat (para o ESP2 saber que este Mega esta online) ----
unsigned long ultimoPing = 0;
const unsigned long INTERVALO_PING = 2000; // manda "estou vivo" a cada 2s

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

/// LCD
void atualizarLCD() {
  lcd.setCursor(0, 3);
  for (int i = 0; i < MAX_CHARS; i++) {
    lcd.print(i < posicao ? textoDigitado[i] : ' ');
  }
}

void limparEntrada() {
  posicao = 0;
  textoDigitado[0] = '\0';
}

void verificarCodigo() {
  if (strncmp(textoDigitado, codigo, TAM_CODIGO) == 0) {
    lcd.setCursor(0, 3);
    lcd.print("Codigo correto!     ");
    Serial.println("CODIGO CORRETO");
    sendToWifi(String("CORRETO"));
  } else {
    lcd.setCursor(0, 3);
    sendToWifi(String("ERRADO"));
    lcd.print("Codigo errado!      ");
    Serial.println("CODIGO ERRADO");
  }

  delay(2000);
  limparEntrada();
  atualizarLCD();
}

// ---------------- teclado
void verTeclado(){
  char tecla = teclado.read();

  if (tecla != NONE) {
    Serial.println(tecla);

    if (tecla == '#') {
      limparEntrada();
      atualizarLCD();
    } else if (tecla == '*') {
      if (posicao > 0) {
        posicao--;
        textoDigitado[posicao] = '\0';
      }
      atualizarLCD();
      sendToWifi(String(textoDigitado));
    } else if (posicao < MAX_CHARS) {
      textoDigitado[posicao] = tecla;
      posicao++;
      textoDigitado[posicao] = '\0';
      atualizarLCD();

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
  Wire.begin();
  Wire.setClock(100000);
  lcd.init();
  lcd.backlight();
  teclado.begin();

  lcd.setCursor(0, 0);
  lcd.print("Hey JATO");
  lcd.setCursor(0, 2);
  lcd.print("Digite o codigo:");

  Serial.begin(115200);
  Serial3.begin(115200);

  WiFi.init(&Serial3);

  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("Modulo WiFi nao encontrado. Verifica os jumpers da placa.");
    while (true);
  }

  Serial.print("A ligar a: ");
  Serial.println(ssid);
  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
    delay(1000);
  }
  Serial.println("Ligado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  ultimoPing = millis();
}

void loop() {
  verTeclado();

  // Heartbeat: manda um ping periodico mesmo que ninguem esteja a mexer no teclado
  if (millis() - ultimoPing >= INTERVALO_PING) {
    sendPing();
    ultimoPing = millis();
  }
}