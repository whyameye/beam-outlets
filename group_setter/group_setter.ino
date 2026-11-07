// Group setter for beam_outlet boards, on a Waveshare ESP32-C6-GEEK.
//
// Broadcasts an open Wi-Fi network named "set_group_to_X". Any beam_outlet
// board that boots within range adopts group X, saves it, and flashes its
// outlet X times (group 0 = one long flash).
//
// BOOT button (short press): next group, 0-9, wrapping 9 -> 0. The SSID changes
// immediately. Unplug the setter when done so boards stop seeing it.
//
// Arduino IDE: board "ESP32C6 Dev Module", Tools -> USB CDC On Boot: Enabled
// (for Serial Monitor). Library: "GFX Library for Arduino" (Arduino_GFX).

#include <WiFi.h>
#include <Arduino_GFX_Library.h>

// --- Pins (ESP32-C6-GEEK) ---
// BOOT and backlight per Waveshare's Arduino docs. LCD SPI pins per a Home
// Assistant community config built from Waveshare's ESP-IDF demo; other board
// batches may differ.
const int PIN_BUTTON = 9;    // BOOT, active LOW
const int PIN_BL     = 6;    // LCD backlight
const int PIN_SCLK   = 1;
const int PIN_MOSI   = 2;
const int PIN_DC     = 3;
const int PIN_RST    = 4;
const int PIN_CS     = 5;

const char *SSID_PREFIX = "set_group_to_";   // must match beam_outlet.ino
const uint8_t AP_CHANNEL = 1;

// Colors (RGB565)
const uint16_t C_BLACK  = 0x0000;
const uint16_t C_WHITE  = 0xFFFF;
const uint16_t C_GREEN  = 0x07E0;
const uint16_t C_YELLOW = 0xFFE0;

Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED);
// 135x240 panel, landscape (rotation 1), IPS. Offsets are the common values for
// 1.14" ST7789 panels; adjust if the image is shifted.
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_RST, 1 /*rotation*/, true /*IPS*/,
                                      135, 240, 52, 40, 53, 40);

uint8_t group = 0;
char ssid[20];

void makeSsid() {
  snprintf(ssid, sizeof(ssid), "%s%u", SSID_PREFIX, group);
}

void startAp() {
  makeSsid();
  WiFi.softAPdisconnect(true);
  bool ok = WiFi.softAP(ssid, NULL, AP_CHANNEL);   // open network
  Serial.printf("[setter] broadcasting %s (%s)\n", ssid, ok ? "ok" : "FAILED");
}

void draw() {
  gfx->fillScreen(C_BLACK);

  gfx->setTextColor(C_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(8, 8);
  gfx->print("GROUP");

  gfx->setTextColor(C_GREEN);
  gfx->setTextSize(10);              // ~60x80 px digit
  gfx->setCursor(150, 20);
  gfx->print(group);

  gfx->setTextColor(C_YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(8, 110);
  gfx->print(ssid);

  gfx->setTextColor(C_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(8, 40);
  gfx->print("BOOT = next group");
  gfx->setCursor(8, 54);
  gfx->print("Power-cycle boards");
  gfx->setCursor(8, 66);
  gfx->print("near this to set.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[setter] boot");

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  if (!gfx->begin()) Serial.println("[setter] display init FAILED");

  WiFi.mode(WIFI_AP);
  startAp();
  draw();
}

// Debounced press detection
bool lastRaw = HIGH;
bool stable = HIGH;
uint32_t lastChange = 0;
const uint32_t DEBOUNCE_MS = 30;

void loop() {
  bool raw = digitalRead(PIN_BUTTON);
  uint32_t now = millis();

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = now;
  }
  if (now - lastChange >= DEBOUNCE_MS && raw != stable) {
    stable = raw;
    if (stable == LOW) {                 // pressed
      group = (group + 1) % 10;
      startAp();
      draw();
    }
  }
}
