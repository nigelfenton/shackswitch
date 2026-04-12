/*
 * giga_dummy_890.ino — Dummy TS-890S for ShackSwitch Kenwood CAT testing
 * Arduino Giga R1 WiFi + Giga Display Shield
 *
 * Touchscreen band selector — tap a band button to change the simulated frequency.
 * CAT server on port 60000 responds to IF; FA; ID; commands.
 * Web server on port 80 as a fallback (same band buttons in a browser).
 *
 * Libraries needed (Arduino Library Manager):
 *   Arduino_GigaDisplay_GFX
 *   Arduino_GigaDisplayTouch
 *   WiFi (included with Giga board package)
 *
 * Setup:
 *   1. Set WIFI_SSID and WIFI_PASSWORD below
 *   2. Flash to Giga R1 WiFi
 *   3. IP shown on screen at startup — enter it in ShackSwitch Kenwood config
 *   4. Touch band buttons to simulate radio QSY
 *
 * CAT note:
 *   Mode digit is at position 29 of the IF; response (0-indexed).
 *   This matches the ShackSwitch kenwood.py parser.
 */

#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include <WiFi.h>

// ── User configuration ────────────────────────────────────────────────────────
#define WIFI_SSID     "YourSSID"
#define WIFI_PASSWORD "YourPassword"
#define CAT_PORT      60000
#define WEB_PORT      80

// ── Display ───────────────────────────────────────────────────────────────────
GigaDisplay_GFX        display;
Arduino_GigaDisplayTouch touch;

// Display dimensions in landscape (rotation 1)
static const int DW = 800;
static const int DH = 480;

// RGB565 colours matching ShackSwitch dark theme
#define C_BG      0x18C3   // #111111
#define C_PANEL   0x2104   // #222222
#define C_BORDER  0x294A   // #333333
#define C_GREEN   0x0666   // #00CC44 (close to #00CC66)
#define C_WHITE   0xFFFF
#define C_MUTED   0x8410   // #888888
#define C_AMBER   0xFC40   // #FF8800
#define C_ACTIVE  0x0329   // #1A3329 (green-tinted dark)
#define C_RED     0xF800

// ── Band table ────────────────────────────────────────────────────────────────
struct Band { const char* name; long long freq_hz; uint8_t mode; };
// mode: 1=LSB 2=USB 3=CW

const Band BANDS[] = {
  { "160m",  1825000LL, 1 },
  { "80m",   3750000LL, 1 },
  { "60m",   5357000LL, 2 },
  { "40m",   7100000LL, 1 },
  { "30m",  10125000LL, 3 },
  { "20m",  14225000LL, 2 },
  { "17m",  18130000LL, 2 },
  { "15m",  21280000LL, 2 },
  { "12m",  24950000LL, 2 },
  { "10m",  28500000LL, 2 },
  { "6m",   50125000LL, 2 },
};
const int NUM_BANDS = 11;

const char* MODE_STR[] = { "?", "LSB", "USB", "CW", "FM", "AM", "FSK", "CW-R", "?", "FSK-R" };

int currentBand = 5;   // Default: 20m

// ── Button layout ─────────────────────────────────────────────────────────────
// 4 columns × 3 rows below a header area
static const int HDR_H   = 100;
static const int COLS    = 4;
static const int ROWS    = 3;
static const int PAD     = 6;
static const int BTN_W   = (DW - PAD * (COLS + 1)) / COLS;          // ~188px
static const int BTN_H   = (DH - HDR_H - PAD * (ROWS + 1)) / ROWS; // ~119px

struct Rect { int x, y, w, h; };
Rect btnRects[NUM_BANDS];

WiFiServer catServer(CAT_PORT);
WiFiServer webServer(WEB_PORT);

// ── Build Kenwood IF; response ────────────────────────────────────────────────
// Format puts mode digit at position 29 (0-indexed from 'I') to match kenwood.py parser.
//   IF + freq(11) + spaces(5) + +(10 zeros) + mode(1) + zeros + ;
//   pos: 0   2         13        18               29
String buildIF() {
  char buf[48];
  snprintf(buf, sizeof(buf),
    "IF%011lld     +0000000000%d0000000;",
    BANDS[currentBand].freq_hz,
    (int)BANDS[currentBand].mode);
  return String(buf);
}

// ── Draw frequency header ─────────────────────────────────────────────────────
void drawHeader() {
  display.fillRect(0, 0, DW, HDR_H, C_BG);
  display.drawFastHLine(0, HDR_H - 1, DW, C_BORDER);

  // Large frequency
  long long f   = BANDS[currentBand].freq_hz;
  int       mhz = f / 1000000;
  int       khz = (f % 1000000) / 1000;
  int       hz  = f % 1000;
  char freqBuf[24];
  snprintf(freqBuf, sizeof(freqBuf), "%d.%03d.%03d MHz", mhz, khz, hz);

  display.setTextColor(C_GREEN);
  display.setTextSize(4);
  display.setCursor(16, 10);
  display.print(freqBuf);

  // Band name + mode
  display.setTextColor(C_WHITE);
  display.setTextSize(3);
  display.setCursor(16, 64);
  display.print(BANDS[currentBand].name);

  display.setTextColor(C_AMBER);
  display.setCursor(110, 64);
  uint8_t m = BANDS[currentBand].mode;
  display.print((m <= 9) ? MODE_STR[m] : "?");

  // Status dot + "CAT ready"
  display.fillCircle(DW - 24, HDR_H / 2, 8, C_GREEN);
  display.setTextColor(C_MUTED);
  display.setTextSize(1);
  display.setCursor(DW - 90, HDR_H / 2 - 4);
  display.print("CAT ready");
}

// ── Draw one band button ──────────────────────────────────────────────────────
void drawBtn(int i) {
  if (i >= NUM_BANDS) return;
  const Rect& r = btnRects[i];
  bool active = (i == currentBand);

  display.fillRoundRect(r.x, r.y, r.w, r.h, 8,
                        active ? C_ACTIVE : C_PANEL);
  display.drawRoundRect(r.x, r.y, r.w, r.h, 8,
                        active ? C_GREEN  : C_BORDER);

  // Centre text in button
  int tsize = 3;
  int cw    = 18;  // approx char width at size 3 (6px × 3)
  int ch    = 24;  // char height at size 3 (8px × 3)
  int len   = strlen(BANDS[i].name);
  int tx    = r.x + (r.w - len * cw) / 2;
  int ty    = r.y + (r.h - ch) / 2;

  display.setTextColor(active ? C_GREEN : C_WHITE);
  display.setTextSize(tsize);
  display.setCursor(tx, ty);
  display.print(BANDS[i].name);
}

void drawAll() {
  display.fillScreen(C_BG);
  drawHeader();
  for (int i = 0; i < NUM_BANDS; i++) drawBtn(i);
}

// ── Touch coordinate mapping ──────────────────────────────────────────────────
// GT911 reports in native portrait space (0..480 × 0..800).
// With setRotation(1) (landscape), remap to logical (0..800 × 0..480).
// If buttons don't respond correctly, swap or negate the mapping here.
void remapTouch(int raw_x, int raw_y, int& lx, int& ly) {
  lx = raw_y;           // portrait Y → landscape X
  ly = 480 - raw_x;     // portrait X → landscape Y (mirrored)
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // Display init
  display.begin();
  display.setRotation(1);   // Landscape 800×480
  display.fillScreen(C_BG);
  display.setTextColor(C_MUTED);
  display.setTextSize(2);
  display.setCursor(20, 20);
  display.print("Connecting to WiFi...");
  display.setTextColor(C_WHITE);
  display.setTextSize(1);
  display.setCursor(20, 50);
  display.print(WIFI_SSID);

  // Touch init
  touch.begin();

  // Pre-calculate button rectangles
  for (int i = 0; i < NUM_BANDS; i++) {
    btnRects[i] = {
      PAD + (i % COLS) * (BTN_W + PAD),
      HDR_H + PAD + (i / COLS) * (BTN_H + PAD),
      BTN_W, BTN_H
    };
  }

  // WiFi
  Serial.print("WiFi: "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.fillScreen(C_BG);
    display.setTextColor(C_RED);
    display.setTextSize(3);
    display.setCursor(20, 20);
    display.print("WiFi failed!");
    display.setTextColor(C_MUTED);
    display.setTextSize(2);
    display.setCursor(20, 70);
    display.print("Check SSID/password in sketch");
    Serial.println("\nWiFi failed.");
    while (true) delay(1000);
  }

  catServer.begin();
  webServer.begin();

  IPAddress ip = WiFi.localIP();
  Serial.println(); Serial.print("IP: "); Serial.println(ip);

  // Splash screen
  display.fillScreen(C_BG);
  display.setTextColor(C_GREEN);
  display.setTextSize(4);
  display.setCursor(20, 30);
  display.print("Dummy TS-890S");
  display.setTextColor(C_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 100);
  display.print("IP:  "); display.print(ip);
  display.setCursor(20, 130);
  display.print("CAT: port 60000");
  display.setCursor(20, 160);
  display.print("Web: http://"); display.print(ip);
  display.setTextColor(C_MUTED);
  display.setCursor(20, 210);
  display.print("Starting in 3 seconds...");
  delay(3000);

  drawAll();
  Serial.println("Ready. Touch band buttons or use web page.");
  Serial.println("Serial: type 'band N' (0=160m .. 10=6m) to change band.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

  // ── Touch input ──────────────────────────────────────────────────────────
  GDTpoint_t pts[5];
  uint8_t n = touch.getTouchPoints(pts);
  if (n > 0) {
    int lx, ly;
    remapTouch(pts[0].x, pts[0].y, lx, ly);

    for (int i = 0; i < NUM_BANDS; i++) {
      const Rect& r = btnRects[i];
      if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) {
        if (i != currentBand) {
          int prev  = currentBand;
          currentBand = i;
          drawBtn(prev);
          drawBtn(i);
          drawHeader();
          Serial.print("Touch: "); Serial.println(BANDS[i].name);
        }
        delay(250);   // debounce
        break;
      }
    }
  }

  // ── Serial command: "band N" ──────────────────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("band ")) {
      int b = cmd.substring(5).toInt();
      if (b >= 0 && b < NUM_BANDS) {
        int prev = currentBand;
        currentBand = b;
        drawBtn(prev);
        drawBtn(currentBand);
        drawHeader();
        Serial.print("Band → "); Serial.println(BANDS[currentBand].name);
      }
    }
  }

  // ── CAT server (port 60000) ───────────────────────────────────────────────
  WiFiClient cat = catServer.available();
  if (cat) {
    String req = "";
    unsigned long t = millis();
    while (cat.connected() && millis() - t < 2000) {
      if (cat.available()) {
        char c = cat.read();
        req += c;
        if (c == ';') break;
      }
    }
    req.trim();
    if (req.length() > 0) {
      Serial.print("CAT RX: "); Serial.println(req);
      if (req.indexOf("IF") >= 0) {
        String resp = buildIF();
        cat.print(resp);
        Serial.print("CAT TX: "); Serial.println(resp);
      } else if (req.startsWith("FA")) {
        char buf[20];
        snprintf(buf, sizeof(buf), "FA%011lld;", BANDS[currentBand].freq_hz);
        cat.print(buf);
      } else if (req.startsWith("ID")) {
        cat.print("ID019;");   // TS-890S model ID
      }
    }
    cat.stop();
  }

  // ── Web server (port 80) — browser fallback ───────────────────────────────
  WiFiClient web = webServer.available();
  if (web) {
    String req = "";
    unsigned long t = millis();
    while (web.connected() && millis() - t < 1000) {
      if (web.available()) {
        char c = web.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }
    int end = req.indexOf('\r');
    String line = (end > 0) ? req.substring(0, end) : req;

    if (line.indexOf("GET /band?n=") >= 0) {
      int p = line.indexOf("n=") + 2;
      int b = line.substring(p, line.indexOf(' ', p)).toInt();
      if (b >= 0 && b < NUM_BANDS) {
        int prev = currentBand;
        currentBand = b;
        drawBtn(prev);
        drawBtn(currentBand);
        drawHeader();
        Serial.print("Web: "); Serial.println(BANDS[currentBand].name);
      }
      web.println("HTTP/1.1 302 Found\r\nLocation: /\r\nConnection: close\r\n");
    } else {
      String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Dummy TS-890S</title>"
        "<style>body{background:#111;color:#e8e8e8;font-family:sans-serif;padding:20px}"
        "h1{color:#00cc66}p{color:#888;font-size:14px;margin-bottom:16px}"
        ".f{font-size:28px;font-weight:bold;color:#00cc66;margin-bottom:20px}"
        ".g{display:flex;flex-wrap:wrap;gap:8px}"
        "a{display:inline-block;padding:10px 16px;font-size:15px;background:#222;"
        "color:#e8e8e8;border:1px solid #444;border-radius:4px;text-decoration:none}"
        "a.on{background:#1a3329;color:#00cc66;border-color:#00cc66}</style></head>"
        "<body><h1>Dummy TS-890S</h1>");
      html += "<div class='f'>";
      html += String(BANDS[currentBand].freq_hz / 1000000.0, 3);
      html += " MHz &mdash; ";
      html += BANDS[currentBand].name;
      html += F("</div><p>Tap a band to change the simulated frequency</p><div class='g'>");
      for (int i = 0; i < NUM_BANDS; i++) {
        html += "<a href='/band?n=";
        html += i;
        html += (i == currentBand) ? "' class='on'>" : "'>";
        html += BANDS[i].name;
        html += "</a>";
      }
      html += F("</div></body></html>");
      web.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close");
      web.print("Content-Length: "); web.println(html.length());
      web.println();
      web.print(html);
    }
    delay(5);
    web.stop();
  }
}
