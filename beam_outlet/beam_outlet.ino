// Beam-triggered outlet, ESP8266 (Wemos D1 mini), ESP-NOW broadcast.
//
// Before flashing each board, uncomment exactly ONE role below.
#define ROLE_SENDER
//#define ROLE_RECEIVER

#if defined(ROLE_SENDER) == defined(ROLE_RECEIVER)
#error "Define exactly one of ROLE_SENDER or ROLE_RECEIVER"
#endif

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

// ===================== Shared =====================

const uint8_t RADIO_CH = 1;              // Wi-Fi radio channel, fixed, same on all boards
const uint8_t DEFAULT_GROUP = 0;
const uint32_t SCAN_WINDOW_MS = 10000;
const uint8_t GROUP_MAGIC = 0x5A;        // marks a valid saved group
const uint8_t MSG_MAGIC = 0xA5;          // marks our ESP-NOW packets

const int RELAY_PIN = D1;                // HIGH = outlet on (via 2N2222 to SSR)

const uint32_t FLASH_ON_MS  = 400;
const uint32_t FLASH_OFF_MS = 400;
const uint32_t LONG_MS      = 2000;

uint8_t groupNum = DEFAULT_GROUP;

typedef struct __attribute__((packed)) {
  uint8_t magic;
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

// ===================== Sender =====================
#ifdef ROLE_SENDER

const int BEAM_PIN = D5;                 // LOW = beam broken (internal pull-up)
const int POT_PIN  = A0;

const uint32_t MIN_MS = 1UL * 1000;
const uint32_t MAX_MS = 15UL * 1000;
const uint32_t SEND_INTERVAL_MS = 100;   // heartbeat for receiver failsafe

uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool outletOn = false;
uint32_t onUntil = 0;
uint32_t lastSend = 0;

uint32_t potDelayMs() {
  int raw = analogRead(POT_PIN);         // 0–1023
  return MIN_MS + (uint32_t)((uint64_t)(MAX_MS - MIN_MS) * raw / 1023);
}

void sendState() {
  Msg m = {MSG_MAGIC, groupNum, (uint8_t)(outletOn ? 1 : 0)};
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

  if (digitalRead(BEAM_PIN) == LOW) {    // broken: start/restart timer
    onUntil = now + potDelayMs();
    outletOn = true;
  }
  if (outletOn && (int32_t)(now - onUntil) >= 0) outletOn = false;

  digitalWrite(RELAY_PIN, outletOn ? HIGH : LOW);

  if (outletOn != wasOn || now - lastSend >= SEND_INTERVAL_MS) sendState();
}

#endif

// ===================== Receiver =====================
#ifdef ROLE_RECEIVER

const uint32_t TIMEOUT_MS = 1000;        // failsafe: off if sender goes silent

volatile bool remoteOn = false;
volatile uint32_t lastRx = 0;

void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(Msg)) return;
  Msg m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != MSG_MAGIC || m.group != groupNum) return;   // not ours
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

#endif
