#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

// --- shared block (keep identical in receiver) ---
const uint8_t RADIO_CH = 1;             // Wi-Fi radio channel, fixed
const uint8_t DEFAULT_ID = 0;
const uint32_t SCAN_WINDOW_MS = 10000;
const uint8_t ID_MAGIC = 0x5A;          // marks a valid saved ID

uint8_t pairId = DEFAULT_ID;

typedef struct __attribute__((packed)) {
  uint8_t magic;   // 0xA5
  uint8_t id;
  uint8_t on;
} Msg;

uint8_t loadId() {
  EEPROM.begin(4);
  if (EEPROM.read(0) == ID_MAGIC && EEPROM.read(1) <= 9) return EEPROM.read(1);
  return DEFAULT_ID;
}

void saveId(uint8_t id) {
  if (EEPROM.read(0) == ID_MAGIC && EEPROM.read(1) == id) return;  // unchanged, skip flash write
  EEPROM.write(0, ID_MAGIC);
  EEPROM.write(1, id);
  EEPROM.commit();
}

// Returns 0–9 if "set_channel_to_X" is seen within the window, else -1
int scanForId() {
  uint32_t start = millis();
  while (millis() - start < SCAN_WINDOW_MS) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s.length() == 16 && s.startsWith("set_channel_to_")) {
        char c = s.charAt(15);
        if (c >= '0' && c <= '9') return c - '0';
      }
    }
  }
  return -1;
}

const int RELAY_PIN = D1;
const uint32_t FLASH_ON_MS  = 400;
const uint32_t FLASH_OFF_MS = 400;
const uint32_t LONG_MS      = 2000;

void flashId(uint8_t id) {
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);                         // gap before the count
  if (id == 0) {                       // 0 = one long flash
    digitalWrite(RELAY_PIN, HIGH);
    delay(LONG_MS);
    digitalWrite(RELAY_PIN, LOW);
    return;
  }
  for (uint8_t i = 0; i < id; i++) {
    digitalWrite(RELAY_PIN, HIGH);
    delay(FLASH_ON_MS);
    digitalWrite(RELAY_PIN, LOW);
    delay(FLASH_OFF_MS);
  }
}

void initRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  pairId = loadId();
  int found = scanForId();
  if (found >= 0) {
    pairId = found;
    saveId(pairId);
    flashId(pairId);
  }

  wifi_set_channel(RADIO_CH);   // after scanning, since the scan hops channels
}
// --- end shared block ---

const int BEAM_PIN  = D5;          // assumed: LOW = beam broken
const int POT_PIN   = A0;

const uint32_t MIN_MS = 1UL * 1000;
const uint32_t MAX_MS = 15UL * 1000;
const uint32_t SEND_INTERVAL_MS = 100;

uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool outletOn = false;
uint32_t onUntil = 0;
uint32_t lastSend = 0;

uint32_t potDelayMs() {
  int raw = analogRead(POT_PIN);   // 0–1023
  return MIN_MS + (uint32_t)((uint64_t)(MAX_MS - MIN_MS) * raw / 1023);
}

void sendState() {
  Msg m = {0xA5, pairId, (uint8_t)(outletOn ? 1 : 0)};
  esp_now_send(BROADCAST, (uint8_t *)&m, sizeof(m));
  lastSend = millis();
}

void setup() {
  pinMode(BEAM_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  initRadio();

  if (esp_now_init() != 0) ESP.restart();
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(BROADCAST, ESP_NOW_ROLE_SLAVE, RADIO_CH, NULL, 0);
}

void loop() {
  uint32_t now = millis();
  bool wasOn = outletOn;

  if (digitalRead(BEAM_PIN) == LOW) {
    onUntil = now + potDelayMs();
    outletOn = true;
  }
  if (outletOn && (int32_t)(now - onUntil) >= 0) outletOn = false;

  digitalWrite(RELAY_PIN, outletOn ? HIGH : LOW);

  if (outletOn != wasOn || now - lastSend >= SEND_INTERVAL_MS) sendState();
}
