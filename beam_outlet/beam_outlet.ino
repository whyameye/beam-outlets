// Beam-triggered outlet, ESP8266 (Wemos D1 mini), ESP-NOW broadcast.
//
// Before flashing each board, uncomment exactly ONE role below.
#define ROLE_SENDER
//#define ROLE_RECEIVER

#if defined(ROLE_SENDER) == defined(ROLE_RECEIVER)
#error "Define exactly one of ROLE_SENDER or ROLE_RECEIVER"
#endif

// Serial debug output at 115200 baud. Set to 0 to disable.
#define DEBUG 1
#if DEBUG
  #define DBG(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)
#endif

#ifdef ROLE_SENDER
  const char *ROLE_NAME = "SENDER";
#else
  const char *ROLE_NAME = "RECEIVER";
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
        if (c >= '0' && c <= '9') {
          DBG("[boot] saw SSID %s\n", s.c_str());
          return c - '0';
        }
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
#if DEBUG
  Serial.begin(115200);
  delay(100);
#endif
  DBG("\n[boot] role=%s\n", ROLE_NAME);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  groupNum = loadGroup();
  DBG("[boot] saved group=%u, scanning %lu ms for set_group_to_X\n",
      groupNum, (unsigned long)SCAN_WINDOW_MS);
  int found = scanForGroup();
  if (found >= 0) {
    groupNum = found;
    saveGroup(groupNum);
    DBG("[boot] group set to %u and saved, flashing outlet\n", groupNum);
    flashGroup(groupNum);
  } else {
    DBG("[boot] no setup SSID seen, using group %u\n", groupNum);
  }

  // WiFi.scanNetworks() leaves the radio unable to receive ESP-NOW frames
  // (sending still works). Cycling Wi-Fi off and back on restores reception;
  // see tests/espnow_rx_test. Must happen before esp_now_init().
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);

  wifi_set_channel(RADIO_CH);
  DBG("[boot] radio channel set %u, actual %u, MAC %s\n",
      RADIO_CH, wifi_get_channel(), WiFi.macAddress().c_str());
}

void startEspNow() {
  if (esp_now_init() != 0) {
    DBG("[boot] esp_now_init failed, restarting\n");
    delay(100);
    ESP.restart();
  }
  DBG("[boot] ESP-NOW ready\n");
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

// Debug counters
volatile uint16_t txOk = 0, txFail = 0;  // from send callback
uint16_t txErr = 0;                      // esp_now_send() returned nonzero
uint32_t lastReport = 0;

// Runs in the Wi-Fi stack's context: no Serial output here.
void onSent(uint8_t *mac, uint8_t status) {
  if (status == 0) txOk++; else txFail++;
}

// The ESP8266's ADC is shared with the Wi-Fi radio: calling analogRead()
// continuously (as loop() would while the beam is broken) stops transmission.
// Read the pot at most every POT_READ_MS and reuse the cached value.
const uint32_t POT_READ_MS = 100;
uint32_t potCacheMs = MIN_MS;
uint32_t lastPotRead = 0;
bool potRead = false;

uint32_t potDelayMs() {
  uint32_t now = millis();
  if (!potRead || now - lastPotRead >= POT_READ_MS) {
    int raw = analogRead(POT_PIN);       // 0–1023
    potCacheMs = MIN_MS + (uint32_t)((uint64_t)(MAX_MS - MIN_MS) * raw / 1023);
    lastPotRead = now;
    potRead = true;
  }
  return potCacheMs;
}

void sendState() {
  Msg m = {MSG_MAGIC, groupNum, (uint8_t)(outletOn ? 1 : 0)};
  if (esp_now_send(BROADCAST, (uint8_t *)&m, sizeof(m)) != 0) txErr++;
  lastSend = millis();
}

void setup() {
  pinMode(BEAM_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  initRadio();

  startEspNow();
  int r1 = esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  int r2 = esp_now_add_peer(BROADCAST, ESP_NOW_ROLE_SLAVE, RADIO_CH, NULL, 0);
  int r3 = esp_now_register_send_cb(onSent);
  DBG("[boot] set_self_role=%d add_peer=%d register_send_cb=%d (0 = ok)\n", r1, r2, r3);
  (void)r1; (void)r2; (void)r3;          // unused when DEBUG is 0
}

bool lastBroken = false;

void loop() {
  uint32_t now = millis();
  bool wasOn = outletOn;
  bool broken = (digitalRead(BEAM_PIN) == LOW);

  if (broken) {                          // broken: start/restart timer
    uint32_t d = potDelayMs();
    onUntil = now + d;
    outletOn = true;
    if (!lastBroken) DBG("[sender] beam broken, delay %lu ms\n", (unsigned long)d);
  } else if (lastBroken) {
    DBG("[sender] beam clear\n");
  }
  lastBroken = broken;

  if (outletOn && (int32_t)(now - onUntil) >= 0) outletOn = false;

  digitalWrite(RELAY_PIN, outletOn ? HIGH : LOW);

  if (outletOn != wasOn) DBG("[sender] outlet %s, group %u\n", outletOn ? "ON" : "OFF", groupNum);
  if (outletOn != wasOn || now - lastSend >= SEND_INTERVAL_MS) sendState();

#if DEBUG
  if (now - lastReport >= 1000) {        // once per second
    noInterrupts();
    uint16_t ok = txOk, fail = txFail;
    txOk = 0; txFail = 0;
    interrupts();
    DBG("[sender] last 1 s: sent ok %u, fail %u, send error %u, channel %u\n",
        ok, fail, txErr, wifi_get_channel());
    txErr = 0;
    lastReport = now;
  }
#endif
}

#endif

// ===================== Receiver =====================
#ifdef ROLE_RECEIVER

const uint32_t TIMEOUT_MS = 1000;        // failsafe: off if sender goes silent

volatile bool remoteOn = false;
volatile uint32_t lastRx = 0;
volatile uint16_t rxOurs = 0;            // packets for our group (debug count)
volatile uint16_t rxOther = 0;           // other groups or unrecognized (debug count)

// Runs in the Wi-Fi stack's context: no Serial output here.
void onRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(Msg)) { rxOther++; return; }
  Msg m;
  memcpy(&m, data, sizeof(m));
  if (m.magic != MSG_MAGIC || m.group != groupNum) { rxOther++; return; }   // not ours
  remoteOn = m.on;
  lastRx = millis();
  rxOurs++;
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  initRadio();

  startEspNow();
  int r1 = esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  int r2 = esp_now_register_recv_cb(onRecv);
  DBG("[boot] set_self_role=%d register_recv_cb=%d (0 = ok)\n", r1, r2);
  (void)r1; (void)r2;                    // unused when DEBUG is 0
}

bool lastOn = false;
uint32_t lastReport = 0;

void loop() {
  uint32_t now = millis();
  bool fresh = (now - lastRx < TIMEOUT_MS);
  bool on = remoteOn && fresh;
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);

  if (on != lastOn) {
    DBG("[receiver] outlet %s%s, group %u\n", on ? "ON" : "OFF",
        (!on && remoteOn && !fresh) ? " (timeout: sender silent)" : "", groupNum);
    lastOn = on;
  }

#if DEBUG
  if (now - lastReport >= 1000) {        // once per second
    noInterrupts();
    uint16_t ours = rxOurs, other = rxOther;
    rxOurs = 0; rxOther = 0;
    interrupts();
    DBG("[receiver] last 1 s: %u packets for group %u, %u other, channel %u\n",
        ours, groupNum, other, wifi_get_channel());
    lastReport = now;
  }
#endif
}

#endif
