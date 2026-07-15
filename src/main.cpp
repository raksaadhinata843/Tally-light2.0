#include <Arduino.h>
#include <MODE_UDPWS_32.h>
#include <MODE_TALLYHUB_32.h>

#define PIN_SWITCH 4 

bool statusModeTerakhir = false;

void setup() {
    pinMode(PIN_SWITCH, INPUT_PULLUP);

    statusModeTerakhir = digitalRead(PIN_SWITCH);

    if (statusModeTerakhir == HIGH) {
        setup_mode_th;
    } else {
        setup_mode_udp;
    }
}

void loop() {

    bool statusSekarang = digitalRead(PIN_SWITCH);

    if (statusSekarang != statusModeTerakhir) {
        delay(500);
        ESP.restart();
    }

    if (statusModeTerakhir == HIGH) {
        loop_mode_th;
    } else {
        loop_mode_udp;
    }
}