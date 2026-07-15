#include <MODE_TALLYHUB_32.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

#define PIN 5
#define NUMPIXELS 1

TallyPacket rxPacket;

static const char *ssid = WIFI_SSID;
static const char *password = WIFI_PASSWORD;

static WiFiUDP udp_sw;
static Adafruit_NeoPixel leds_sw(1, 5, NEO_GRB + NEO_KHZ800);

static void recon_sw()
{
  WiFi.reconnect();
  while (WiFi.status() != WL_CONNECTED)
  {
    leds_sw.setPixelColor(0, leds_sw.Color(255, 0, 0));
    leds_sw.show();
    delay(500);
    leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 0));
    leds_sw.show();
    delay(500);
  }
  udp_sw.beginMulticast(IPAddress(239, 1, 2, 3), 4210);
}

void setup_mode_udp()
{
  leds_sw.begin();
  leds_sw.setBrightness(50);
  leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 0));
  leds_sw.show();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    leds_sw.setPixelColor(0, leds_sw.Color(255, 0, 0));
    leds_sw.show();
    delay(500);
    leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 0));
    leds_sw.show();
    delay(500);
  }

  ArduinoOTA.setHostname(("tally-cam" + String(CAM_ID)).c_str());
  ArduinoOTA.begin();

  // Flash putih sebanyak CAM_ID+1 kali agar ESP bisa dikenali
  delay(500);
  for (uint8_t i = 0; i <= CAM_ID; i++)
  {
    leds_sw.setPixelColor(0, leds_sw.Color(255, 255, 255));
    leds_sw.show();
    delay(300);
    leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 0));
    leds_sw.show();
    delay(300);
  }

  // Indikator biru = siap menerima
  leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 255));
  leds_sw.show();

  udp_sw.beginMulticast(IPAddress(239, 1, 2, 3), 4210);
}

void loop()
{
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED)
  {
    recon();
    return;
  }

  int packetSize = udp_sw.parsePacket();
  if (packetSize == sizeof(TallyPacket))
  {
    TallyPacket rxPacket;
    udp_sw.read((unsigned char *)&rxPacket, sizeof(TallyPacket));

    bool isPgm = (rxPacket.pgm_mask & (1 << CAM_ID));
    bool isPvw = (rxPacket.pvw_mask & (1 << CAM_ID));

    if (isPgm)
    {
      leds_sw.setPixelColor(0, leds_sw.Color(255, 0, 0));
    }
    else if (isPvw)
    {
      leds_sw.setPixelColor(0, leds_sw.Color(0, 255, 0));
    }
    else
    {
      leds_sw.setPixelColor(0, leds_sw.Color(0, 0, 255));
    }

    leds_sw.show();
  }
}