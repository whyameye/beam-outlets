#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

// --- shared block (keep identical in sender) ---
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

void initRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  pairId = loadId();
  int found = scanForId();
  if (found >= 0) {
    pairId = found;
    saveId(pairId);
  }

  wifi_set_channel(RADIO_CH);   // after scanning, since the scan hops channels
}
// --- end shared block ---

const int RELAY_PIN = D1;
const uint32_t TIMEOUT_MS = 1000;

volatile bool remoteOn = false;
volatile uint32_t lastRx = 0;

void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(Msg)) return;
  Msg m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != 0xA5 || m.id != pairId) return;
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
