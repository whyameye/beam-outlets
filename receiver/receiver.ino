#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

// --- shared block (keep identical in sender) ---
const uint8_t RADIO_CH = 1;             // Wi-Fi radio channel, fixed, same on all boards
const uint8_t DEFAULT_GROUP = 0;
const uint32_t SCAN_WINDOW_MS = 10000;
const uint8_t GROUP_MAGIC = 0x5A;       // marks a valid saved group

uint8_t groupNum = DEFAULT_GROUP;

typedef struct __attribute__((packed)) {
  uint8_t magic;   // 0xA5
  uint8_t group;
  uint8_t on;
} Msg;

uint8_t loadGroup() {
  EEPROM.begin(4);
  if (EEPROM.read(0) == GROUP_MAGIC && EEPROM.read(1) <= 9) return EEPROM.read(1);
  return DEFAULT_GROUP;
}

void saveGroup(uint8_t g) {
  if (EEPROM.read(0) == GROUP_MAGIC && EEPROM.read(1) == g) return;  // unchanged, skip flash write
  EEPROM.write(0, GROUP_MAGIC);
  EEPROM.write(1, g);
  EEPROM.commit();
}

// Returns 0–9 if "set_group_to_X" is seen within the window, else -1
int scanForGroup() {
  uint32_t start = millis();
  while (millis() - start < SCAN_WINDOW_MS) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s.length() == 14 && s.startsWith("set_group_to_")) {
        char c = s.charAt(13);
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

void flashGroup(uint8_t g) {
  digitalWrite(RELAY_PIN, LOW);
  delay(1000);                         // gap before the count
  if (g == 0) {                        // 0 = one long flash
    digitalWrite(RELAY_PIN, HIGH);
    delay(LONG_MS);
    digitalWrite(RELAY_PIN, LOW);
    return;
  }
  for (uint8_t i = 0; i < g; i++) {
    digitalWrite(RELAY_PIN, HIGH);
    delay(FLASH_ON_MS);
    digitalWrite(RELAY_PIN, LOW);
    delay(FLASH_OFF_MS);
  }
}

void initRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  groupNum = loadGroup();
  int found = scanForGroup();
  if (found >= 0) {
    groupNum = found;
    saveGroup(groupNum);
    flashGroup(groupNum);
  }

  wifi_set_channel(RADIO_CH);   // after scanning, since the scan hops channels
}
// --- end shared block ---

const uint32_t TIMEOUT_MS = 1000;

volatile bool remoteOn = false;
volatile uint32_t lastRx = 0;

void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(Msg)) return;
  Msg m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != 0xA5 || m.group != groupNum) return;
  remoteOn = m.on;
  lastRx = millis();
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  initRadio();

  if (esp_now_init() != 0) ESP.restart();
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onRecv);
}

void loop() {
  bool on = remoteOn && (millis() - lastRx < TIMEOUT_MS);
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}
