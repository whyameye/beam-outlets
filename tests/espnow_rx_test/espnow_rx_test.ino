// ESP-NOW receive test for ESP8266.
//
// Starts like the Random Nerd Tutorials ESP8266 receiver (no scan, no
// disconnect, no set_channel), then applies the main sketch's startup calls
// one at a time and counts frames received for 3 s after each, to find which
// call stops reception. After the last stage it keeps reporting once per second.
// Leave the sender running. Output at 115200 baud.
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

void startEspNow() {
  int r0 = esp_now_init();
  int r1 = esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  int r2 = esp_now_register_recv_cb(onRecv);
  Serial.printf("[rxtest]   init=%d set_self_role=%d register_recv_cb=%d (0 = ok)\n", r0, r1, r2);
}

uint16_t takeCount() {
  noInterrupts();
  uint16_t n = rxCount; rxCount = 0;
  interrupts();
  return n;
}

void measure(const char *stage) {
  takeCount();                 // discard frames from before this stage
  delay(3000);
  uint16_t n = takeCount();
  Serial.printf("[rxtest] %-34s -> %3u frames in 3 s, channel %u\n", stage, n, wifi_get_channel());
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[rxtest] boot");

  WiFi.mode(WIFI_STA);
  Serial.printf("[rxtest] MAC %s, channel %u\n", WiFi.macAddress().c_str(), wifi_get_channel());
  startEspNow();
  measure("0 baseline (expect ~30)");

  WiFi.disconnect();
  measure("1 after WiFi.disconnect()");

  wifi_set_channel(1);
  measure("2 after wifi_set_channel(1)");

  int n = WiFi.scanNetworks();
  Serial.printf("[rxtest]   scan found %d networks\n", n);
  measure("3 after WiFi.scanNetworks()");

  wifi_set_channel(1);
  measure("4 after wifi_set_channel(1) again");

  esp_now_deinit();
  startEspNow();
  measure("5 after ESP-NOW deinit + init");

  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  startEspNow();
  measure("6 after WIFI_OFF -> STA + ESP-NOW init");

  Serial.println("[rxtest] stages done, reporting every second");
}

uint32_t lastReport = 0;

void loop() {
  if (millis() - lastReport >= 1000) {
    uint16_t n = takeCount();
    Serial.printf("[rxtest] last 1 s: %u frames, channel %u", n, wifi_get_channel());
    if (n) Serial.printf(", last from %02X:%02X:%02X:%02X:%02X:%02X len %u data %02X %02X %02X",
                         lastMac[0], lastMac[1], lastMac[2], lastMac[3], lastMac[4], lastMac[5],
                         lastLen, lastData[0], lastData[1], lastData[2]);
    Serial.println();
    lastReport = millis();
  }
}
