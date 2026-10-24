#include <Arduino.h>


const uint8_t pinos[] = {16, 17, 18, 19, 21, 22, 23, 26, 27, 32, 33};

void setup() {
  Serial.begin(115200);
  for (uint8_t p : pinos) pinMode(p, INPUT_PULLUP);
}

void loop() {
  for (uint8_t p : pinos) {
    Serial.printf("GPIO%d=%d  ", p, digitalRead(p));
  }
  Serial.println();
  delay(1000);
}