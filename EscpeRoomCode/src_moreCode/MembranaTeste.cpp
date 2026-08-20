#include <Arduino.h>
#include <Keypad.h>

Keypad teclado;

void setup() {
  Serial.begin(115200);
  teclado.begin();  // <-- ESTA LINHA É ESSENCIAL, faltava no código anterior
  Serial.println("Teclado ZRX543 pronto...");
}

void loop() {
  char tecla = teclado.read();
  if (tecla) {
    Serial.print("Tecla pressionada: ");
    Serial.println(tecla);
  }
  delay(1000);
}