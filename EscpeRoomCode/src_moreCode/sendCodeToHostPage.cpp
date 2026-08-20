#include <Arduino.h>
#include <WiFiEsp.h>

char ssid[] = "GP_wifi";
char pass[] = "qwerty123";

char server[] = "192.168.4.22"; // IP do C6

WiFiEspClient client;
int status = WL_IDLE_STATUS;

void setup() {
  Serial.begin(115200);
  Serial3.begin(115200); // ESP8266 embutido fala por aqui

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
}

void loop() {
  if (client.connect(server, 80)) {
    String valor = String(millis()); // troca pelo valor real depois
    String req = "GET /receber?valor=" + valor + " HTTP/1.1\r\n";
    req += "Host: " + String(server) + "\r\n";
    req += "Connection: close\r\n\r\n";

    client.print(req);
    Serial.println("Enviado: " + valor);
    client.stop();
  } else {
    Serial.println("Falha a ligar ao servidor.");
  }
  delay(2000);
}
