#include <ESP8266WiFi.h>
#include <espnow.h>

const uint8_t PAIR_ID  = 3;
const uint8_t RADIO_CH = 1;
const int RELAY_PIN = D1;
const uint32_t TIMEOUT_MS = 1000;      // failsafe: off if sender goes silent

typedef struct __attribute__((packed)) {
  uint8_t magic;
  uint8_t id;
  uint8_t on;
} Msg;

volatile bool remoteOn = false;
volatile uint32_t lastRx = 0;

void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(Msg)) return;
  Msg m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != 0xA5 || m.id != PAIR_ID) return;   // not ours
  remoteOn = m.on;
  lastRx = millis();
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(RADIO_CH);

  if (esp_now_init() != 0) ESP.restart();
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onRecv);
}

void loop() {
  bool on = remoteOn && (millis() - lastRx < TIMEOUT_MS);
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}
