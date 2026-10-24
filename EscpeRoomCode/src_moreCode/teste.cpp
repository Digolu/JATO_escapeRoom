#include <Wire.h>
#include <Arduino.h>


void setup() {
  Serial.begin(115200);
  Wire.begin(14, 15); // usa os teus PINO_SDA/PINO_SCL atuais
  Wire.setClock(100000);
  delay(1000);
  Serial.println("A procurar dispositivos I2C...");
}

void loop() {
  byte encontrados = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Dispositivo encontrado em 0x");
      Serial.println(addr, HEX);
      encontrados++;
    }
  }
  if (encontrados == 0) Serial.println("Nenhum dispositivo encontrado.");
  delay(3000);
}