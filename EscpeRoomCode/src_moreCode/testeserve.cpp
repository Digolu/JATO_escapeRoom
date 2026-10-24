#include <Arduino.h>
#include <ESP32Servo.h>

#define PINO_SERVO 13
const int ANGULO_FECHADO = 0;
const int ANGULO_ABERTO  = 180;
const unsigned long INTERVALO = 2000;  // 2 segundos

Servo servo;
bool aberto = false;
unsigned long ultimaTroca = 0;

void setup() {
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo.setPeriodHertz(50);
  servo.attach(PINO_SERVO, 500, 2500);
  servo.write(ANGULO_FECHADO);
}

void loop() {
  if (millis() - ultimaTroca >= INTERVALO) {
    ultimaTroca = millis();
    aberto = !aberto;
    servo.write(aberto ? ANGULO_ABERTO : ANGULO_FECHADO);
    Serial.println(aberto ? "Servo ABERTO (180)" : "Servo FECHADO (0)");
  }
}