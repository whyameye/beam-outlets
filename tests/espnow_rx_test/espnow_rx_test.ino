// Minimal ESP-NOW receive test for ESP8266, modeled on the Random Nerd
// Tutorials ESP8266 receiver: no Wi-Fi scan, no disconnect, no set_channel.
// Prints every ESP-NOW frame received, from any sender, at 115200 baud.
#include <ESP8266WiFi.h>
#include <espnow.h>

volatile uint16_t rxCount = 0;
volatile uint8_t lastLen = 0;
volatile uint8_t lastMac[6];
volatile uint8_t lastData[3];

void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  rxCount++;
  lastLen = len;
  for (int i = 0; i < 6; i++) lastMac[i] = mac[i];
  for (int i = 0; i < 3 && i < len; i++) lastData[i] = data[i];
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[rxtest] boot");

  WiFi.mode(WIFI_STA);
  Serial.printf("[rxtest] MAC %s, channel %u\n", WiFi.macAddress().c_str(), wifi_get_channel());

  int r0 = esp_now_init();
  int r1 = esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  int r2 = esp_now_register_recv_cb(onRecv);
  Serial.printf("[rxtest] init=%d set_self_role=%d register_recv_cb=%d (0 = ok)\n", r0, r1, r2);
}

uint32_t lastReport = 0;

void loop() {
  if (millis() - lastReport >= 1000) {
    noInterrupts();
    uint16_t n = rxCount; rxCount = 0;
    interrupts();
    Serial.printf("[rxtest] last 1 s: %u frames, channel %u", n, wifi_get_channel());
    if (n) Serial.printf(", last from %02X:%02X:%02X:%02X:%02X:%02X len %u data %02X %02X %02X",
                         lastMac[0], lastMac[1], lastMac[2], lastMac[3], lastMac[4], lastMac[5],
                         lastLen, lastData[0], lastData[1], lastData[2]);
    Serial.println();
    lastReport = millis();
  }
}
