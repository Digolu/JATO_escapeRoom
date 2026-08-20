#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

const uint16_t kRecvPin = 2; // Posição do pino OUT do sensor
IRrecv irrecv(kRecvPin);
decode_results results;
int IRrecived;

// =========================================================================
// PREENCHE AQUI COM OS TEUS CÓDIGOS HEXADECIMAIS (Substitui os 0XFF)
// =========================================================================
const uint64_t CODIGO_1 = 0xFF30CF; // Exemplo para a tecla 1
const uint64_t CODIGO_2 = 0xFF18E7;
const uint64_t CODIGO_3 = 0xFF7A85;
const uint64_t CODIGO_4 = 0XFF10EF;
const uint64_t CODIGO_5 = 0XFF38C7;
const uint64_t CODIGO_6 = 0XFF5AA5;
const uint64_t CODIGO_7 = 0XFF42BD;
const uint64_t CODIGO_8 = 0XFF4AB5;
const uint64_t CODIGO_9 = 0XFF52AD;
const uint64_t CODIGO_TECLA_LONGA = 0xFFFFFFFFFFFFFFFF;


int obtermNumeroTeclado() {
    if (irrecv.decode(&results)) {
        uint64_t valorLido = results.value;
        
        //Serial.print("Código HEX Lido: 0x");
        //serialPrintUint64(valorLido, HEX);
        //Serial.println();

        irrecv.resume(); 

        if (valorLido == CODIGO_1) return 1;
        if (valorLido == CODIGO_2) return 2;
        if (valorLido == CODIGO_3) return 3;
        if (valorLido == CODIGO_4) return 4;
        if (valorLido == CODIGO_5) return 5;
        if (valorLido == CODIGO_6) return 6;
        if (valorLido == CODIGO_7) return 7;
        if (valorLido == CODIGO_8) return 8;
        if (valorLido == CODIGO_9) return 9;
        if (valorLido == CODIGO_TECLA_LONGA) {
            Serial.println(">>> Tecla longa detetada!");
            return 99; 
        }
    }
    
    return 0; 
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    irrecv.enableIRIn();
    Serial.println("Receptor IR Ativo. Pressiona as teclas...");
}

void loop() {
    
    IRrecived = obtermNumeroTeclado();

    if (IRrecived > 0) {
        Serial.print(">>> Tecla detetada: ");
        Serial.println(IRrecived);
    }
    if (IRrecived == 99) {
        Serial.println(">>> Tecla longa detetada!");
    }

    yield(); 
}