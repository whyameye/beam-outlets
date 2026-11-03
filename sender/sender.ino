#include <ESP8266WiFi.h>
#include <espnow.h>

const uint8_t PAIR_ID  = 3;        // 1–6, must match the paired receiver
const uint8_t RADIO_CH = 1;        // Wi-Fi radio channel, same on all 12 boards

const int BEAM_PIN  = D5;          // assumed: LOW = beam broken
const int RELAY_PIN = D1;          // assumed: HIGH = outlet on
const int POT_PIN   = A0;          // D1 mini's only ADC

const uint32_t MIN_MS = 5UL * 1000;
const uint32_t MAX_MS = 300UL * 1000;
const uint32_t SEND_INTERVAL_MS = 100;

uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct __attribute__((packed)) {
  uint8_t magic;   // 0xA5
  uint8_t id;
  uint8_t on;
} Msg;

bool outletOn = false;
uint32_t onUntil = 0;
uint32_t lastSend = 0;

uint32_t potDelayMs() {
  int raw = analogRead(POT_PIN);   // 0–1023
  return MIN_MS + (uint32_t)((uint64_t)(MAX_MS - MIN_MS) * raw / 1023);
}

void sendState() {
  Msg m = {0xA5, PAIR_ID, (uint8_t)(outletOn ? 1 : 0)};
  esp_now_send(BROADCAST, (uint8_t *)&m, sizeof(m));
  lastSend = millis();
}

void setup() {
  pinMode(BEAM_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(RADIO_CH);

  if (esp_now_init() != 0) ESP.restart();
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(BROADCAST, ESP_NOW_ROLE_SLAVE, RADIO_CH, NULL, 0);
}

void loop() {
  uint32_t now = millis();
  bool wasOn = outletOn;

  if (digitalRead(BEAM_PIN) == LOW) {          // broken: start/restart timer
    onUntil = now + potDelayMs();
    outletOn = true;
  }
  if (outletOn && (int32_t)(now - onUntil) >= 0) outletOn = false;

  digitalWrite(RELAY_PIN, outletOn ? HIGH : LOW);

  if (outletOn != wasOn || now - lastSend >= SEND_INTERVAL_MS) sendState();
}
