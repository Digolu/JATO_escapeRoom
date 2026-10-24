#include <Arduino.h>
#include <WiFi.h>

const char* ssid = "GP_wifi";
const char* password = "qwerty123";

WiFiClient client;

void setup() {

    Serial.begin(115200);

    // criar a rede wifi

    WiFi.softAP(ssid,password);

    Serial.println("Wifi criado");
    Serial.print("IP: ");
    Serial.print(WiFi.softAPIP());
    
}

void loop() {

 }