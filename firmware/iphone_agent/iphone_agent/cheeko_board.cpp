// Cheeko Gotchi board layer, see cheeko_board.h. The status screen is drawn into a PSRAM canvas with
// Adafruit GFX and pushed to the JD9853 panel over SPI only when its text changes.
#include "cheeko_board.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ble_link.h"
#include "netcfg.h"
#include "video_link.h"
#include "src/airplay/airplay.h"

static constexpr int PIN_POWER_OFF = 2;  // HIGH cuts the power: hold it LOW (SKILL.md, Gotcha 8)
static constexpr int PIN_POWER_KEY = 3;  // middle button, active HIGH, external pulldown
static constexpr int PIN_PA_CTRL = 4;    // speaker amp, unused here: keep it off
static constexpr int PIN_LCD_DC = 8;
static constexpr int PIN_LCD_SCLK = 9;
static constexpr int PIN_LCD_MOSI = 10;
static constexpr int PIN_LCD_BL = 13;
static constexpr int PIN_LCD_CS = 14;
static constexpr int PIN_LCD_RST = 17;
static constexpr int LCD_W = 240, LCD_H = 296;

static const SPISettings LCD_SPI(40000000, MSBFIRST, SPI_MODE0);

static void lcdCmd(uint8_t cmd, const uint8_t *data = nullptr, size_t n = 0) {
  SPI.beginTransaction(LCD_SPI);
  digitalWrite(PIN_LCD_CS, LOW);
  digitalWrite(PIN_LCD_DC, LOW);
  SPI.write(cmd);
  if (n) {
    digitalWrite(PIN_LCD_DC, HIGH);
    SPI.writeBytes(data, n);
  }
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

// JD9853 2.01" 240x296 init, as in firmware/hardware_test/main/cheeko_self_test.c (JD9853_PANEL_INIT).
struct InitCmd {
  uint8_t cmd, len;
  uint8_t data[32];
  uint16_t delay_ms;
};
static const InitCmd PANEL_INIT[] = {
    {0xDF, 2, {0x98, 0x53}, 0},
    {0xDE, 1, {0x00}, 0},
    {0xB2, 1, {0x25}, 0},
    {0xB7, 4, {0x00, 0x29, 0x00, 0x51}, 0},
    {0xBB, 6, {0x4F, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 0},
    {0xC0, 2, {0x44, 0xA4}, 0},
    {0xC1, 1, {0x12}, 0},
    {0xC3, 8, {0x7D, 0x07, 0x14, 0x06, 0xC8, 0x71, 0x6C, 0x77}, 0},
    {0xC4, 12, {0x00, 0x00, 0x94, 0x79, 0x25, 0x0A, 0x16, 0x79, 0x25, 0x0A, 0x16, 0x82}, 0},
    {0xC8, 32, {0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00,
                0x3F, 0x34, 0x2D, 0x26, 0x2B, 0x2B, 0x25, 0x24, 0x23, 0x22, 0x20, 0x17, 0x14, 0x0E, 0x06, 0x00}, 0},
    {0xD0, 5, {0x04, 0x06, 0x6B, 0x0F, 0x00}, 0},
    {0xD7, 2, {0x00, 0x30}, 0},
    {0xE6, 1, {0x10}, 0},
    {0xDE, 1, {0x01}, 0},
    {0xB7, 5, {0x03, 0x13, 0xEF, 0x35, 0x35}, 0},
    {0xC1, 3, {0x14, 0x15, 0xC0}, 0},
    {0xC2, 2, {0x06, 0x3A}, 0},
    {0xC4, 2, {0x72, 0x12}, 0},
    {0xBE, 1, {0x00}, 0},
    {0xDE, 1, {0x00}, 0},
    {0x35, 1, {0x00}, 0},
    {0x3A, 1, {0x05}, 0},                    // RGB565
    {0x2A, 4, {0x00, 0x00, 0x00, 0xEF}, 0},  // CASET 0..239
    {0x2B, 4, {0x00, 0x00, 0x01, 0x27}, 0},  // RASET 0..295
    {0x11, 0, {}, 120},                      // sleep out
    {0x29, 0, {}, 10},                       // display on
};

static void lcdInit() {
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_DC, OUTPUT);
  pinMode(PIN_LCD_RST, OUTPUT);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);
  digitalWrite(PIN_LCD_BL, LOW);  // backlight off until the first frame is drawn
  SPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);  // no MISO on this board

  digitalWrite(PIN_LCD_RST, LOW);
  delay(10);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(10);
  lcdCmd(0x11);  // sleep out, then MADCTL and COLMOD, as esp_lcd's panel init does
  delay(100);
  const uint8_t madctl = 0x08, colmod = 0x55;  // BGR order matches this panel; RGB565
  lcdCmd(0x36, &madctl, 1);
  lcdCmd(0x3A, &colmod, 1);
  for (const InitCmd &c : PANEL_INIT) {
    lcdCmd(c.cmd, c.data, c.len);
    if (c.delay_ms) delay(c.delay_ms);
  }
}

static void lcdPush(const uint16_t *px) {
  const uint8_t cols[] = {0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xff};
  const uint8_t rows[] = {0, 0, (LCD_H - 1) >> 8, (LCD_H - 1) & 0xff};
  lcdCmd(0x2A, cols, 4);
  lcdCmd(0x2B, rows, 4);
  static uint8_t line[LCD_W * 2];  // internal RAM; the panel wants big-endian RGB565
  SPI.beginTransaction(LCD_SPI);
  digitalWrite(PIN_LCD_CS, LOW);
  digitalWrite(PIN_LCD_DC, LOW);
  SPI.write(0x2C);  // RAMWR
  digitalWrite(PIN_LCD_DC, HIGH);
  for (int y = 0; y < LCD_H; y++, px += LCD_W) {
    for (int x = 0; x < LCD_W; x++) {
      line[2 * x] = px[x] >> 8;
      line[2 * x + 1] = px[x] & 0xff;
    }
    SPI.writeBytes(line, sizeof line);
  }
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

// ---------------------------------------------------------------- state shared with the loop task

static portMUX_TYPE noteLock = portMUX_INITIALIZER_UNLOCKED;
static char lastAction[24] = "-";

void board_note(const String &action) {
  portENTER_CRITICAL(&noteLock);
  strlcpy(lastAction, action.c_str(), sizeof lastAction);
  portEXIT_CRITICAL(&noteLock);
}

static String noteRead() {  // copy out first: no allocation inside the critical section
  char action[sizeof lastAction];
  portENTER_CRITICAL(&noteLock);
  memcpy(action, lastAction, sizeof action);
  portEXIT_CRITICAL(&noteLock);
  return action;
}

bool board_stop_pressed() {
  static bool was = false;
  static uint32_t changedAt = 0;
  bool down = digitalRead(PIN_POWER_KEY) == HIGH;
  if (down == was || millis() - changedAt < 30) return false;  // 30 ms debounce
  was = down;
  changedAt = millis();
  return down;
}

void board_begin() {
  pinMode(PIN_POWER_OFF, OUTPUT);
  digitalWrite(PIN_POWER_OFF, LOW);
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, LOW);
  pinMode(PIN_POWER_KEY, INPUT);
  lcdInit();
}

// ---------------------------------------------------------------- status screen

static constexpr uint16_t C_BG = 0x0000, C_DIM = 0x8410, C_GOOD = 0x5F36 /* mint */, C_WAIT = 0xFE8C /* amber */,
                          C_TITLE = 0x4EBF /* cyan */, C_INK = 0xF7DF;

static GFXcanvas16 *canvas;

static void row(int y, const char *label, const String &value, bool good) {
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(12, y);
  canvas->print(label);
  canvas->setTextSize(2);
  canvas->setTextColor(good ? C_GOOD : C_WAIT);
  canvas->setCursor(12, y + 11);
  canvas->print(value);
}

static String jsonField(const String &json, const char *key) {  // flat string fields of net_json()
  String k = String("\"") + key + "\":\"";
  int a = json.indexOf(k);
  if (a < 0) return "";
  a += k.length();
  int b = json.indexOf('"', a);
  return b < 0 ? "" : json.substring(a, b);
}

// Everything the screen shows, as one string: redraw only when it changes.
static String snapshot() {
  String net = net_json();
  VideoStats v = video_stats();
  return String(ble_connected()) + "|" + usbState() + "|" + net_mode() + "|" + jsonField(net, "ssid") + "|" +
         jsonField(net, "pass") + "|" + airplay_state() + "|" + (int)(v.fps + 0.5f) + "|" + noteRead();
}

static void draw() {
  canvas->fillScreen(C_BG);
  canvas->setTextWrap(false);
  canvas->setTextSize(2);
  canvas->setTextColor(C_TITLE);
  canvas->setCursor(12, 12);
  canvas->print("iPhone Agent");
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(12, 32);
  canvas->print("ESP32 Agent " + board_suffix());
  canvas->drawFastHLine(12, 46, LCD_W - 24, C_DIM);

  bool app = ble_connected();
  row(56, "PHONE APP (BLE)", app ? "linked" : "waiting", app);
  String usb = usbState();
  row(90, "USB MOUSE + KEYBOARD", usb == "ok" ? "ok" : usb == "asleep" ? "unlock phone" : "plug in", usb == "ok");

  String net = net_json(), mode = net_mode();
  bool ap = mode == "ap";
  row(124, ap ? "HOTSPOT" : "WI-FI", jsonField(net, "ssid"), net_up());
  if (ap) {
    canvas->setTextSize(1);
    canvas->setTextColor(C_INK);
    canvas->setCursor(12, 145);
    canvas->print("pass " + jsonField(net, "pass"));
  }

  static const char *mirror[] = {"not mirroring", "connected", "streaming"};
  int s = airplay_state();
  String m = mirror[s];
  if (s == AIRPLAY_STREAMING) m += " " + String((int)(video_stats().fps + 0.5f)) + "fps";
  row(162, "SCREEN MIRRORING", m, s == AIRPLAY_STREAMING);

  row(196, "LAST ACTION", noteRead(), true);

  canvas->drawFastHLine(12, 250, LCD_W - 24, C_DIM);
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(12, 260);
  canvas->print("middle button: stop the agent");
  canvas->setCursor(12, 274);
  canvas->print("based on ESP32 iPhone Agent");
}

static void uiTask(void *) {
  String shown;
  bool lit = false;
  for (;;) {
    String now = snapshot();
    if (now != shown) {
      draw();
      lcdPush(canvas->getBuffer());
      shown = now;
      if (!lit) {
        digitalWrite(PIN_LCD_BL, HIGH);
        lit = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void board_ui_start() {
  canvas = new GFXcanvas16(LCD_W, LCD_H);  // 142 KB: lands in PSRAM
  if (!canvas->getBuffer()) {
    Serial.println("ui: no memory for the screen canvas");
    return;
  }
  xTaskCreatePinnedToCore(uiTask, "ui", 4096, NULL, 1, NULL, 1);
}
