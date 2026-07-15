#ifndef MODE_UDPWS_32_H
#define MODE_UDPWS_32_H

#include <Arduino.h>

// Struktur data dari kode asli lu
struct TallyPacket {
    uint8_t pgm_mask;
    uint8_t pvw_mask;
};

// Definisikan variabel luar yang dibutuhkan (misal didefinisikan di main)
extern const uint8_t CAM_ID; 

void setup_mode_udp();
void loop_mode_udp();

#endif