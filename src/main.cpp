#include <Arduino.h>
#include <MODE_UDPWS.h>
#include <MODE_TALLYHUB.h>

#define PIN_SWITCH 4 

bool statusModeTerakhir;
bool mode_udp = true;

void setup() {
    pinMode(PIN_SWITCH, INPUT_PULLUP);

    statusModeTerakhir = digitalRead(PIN_SWITCH);

    setup_mode_udp();
}

void loop() {

    bool statusSekarang = digitalRead(PIN_SWITCH);

    if (statusSekarang != statusModeTerakhir) {
        delay(500);
        
        if (digitalRead(PIN_SWITCH) == statusSekarang) {

            if (mode_udp) {
                mode_udp = false;
                setup_mode_th();
            } else {
                mode_udp = true;
                setup_mode_udp();
            }
        }
    }
    
    statusModeTerakhir = statusSekarang;

    if (!mode_udp) {
        loop_mode_th();
    } else {
        loop_mode_udp();
    }
}