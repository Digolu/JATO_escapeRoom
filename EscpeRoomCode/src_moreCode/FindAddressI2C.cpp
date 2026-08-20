#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define SDA_PIN 21
#define SCL_PIN 22

// Ajusta o endereco conforme o scanner encontrou (0x27 ou 0x3F)
LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  //Wire.begin(SDA_PIN, SCL_PIN); // importante no ESP32
  Wire.begin(); // mega
  lcd.init();
  lcd.backlight();

  lcd.setCursor(5, 0);
  lcd.print("HEY JATO!");

  lcd.setCursor(4, 1);
  lcd.print("ESCAPE ROOM");

  lcd.setCursor(2, 2);
  lcd.print("ESTA DE VOLTA!!!");

  lcd.setCursor(2, 3);
  lcd.print("LETS BRAINSTORM!");
}

void loop() {

 }