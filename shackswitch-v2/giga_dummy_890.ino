/*
 * giga_dummy_890.ino — Dummy TS-890S + ser2net USB bridge
 * Arduino Giga R1 WiFi + Giga Display Shield
 *
 * Port 60000 — Dummy TS-890S Kenwood CAT server (touchscreen band select)
 * Port 60001 — Transparent ser2net bridge: TCP ↔ USB-A host serial port
 *              Connect any radio (TS-450S, IC-9700, etc.) to the USB-A port.
 *              ShackSwitch sees it as a network radio at <giga-ip>:60001.
 *
 * Libraries needed (Arduino Library Manager):
 *   Arduino_GigaDisplay_GFX
 *   Arduino_GigaDisplayTouch
 *   Arduino_USBHostMbed5
 *   WiFi (included with Giga board package)
 *
 * Setup:
 *   1. Set WIFI_SSID, WIFI_PASSWORD and BRIDGE_BAUD below
 *   2. Flash to Giga R1 WiFi
 *   3. IP shown on screen — use <ip>:60000 for dummy 890, <ip>:60001 for bridge
 *   4. Plug physical radio into the Giga USB-A host port
 */

#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "Arduino_USBHostMbed5.h"
#include <WiFi.h>

// ── User configuration ────────────────────────────────────────────────────────
#define WIFI_SSID     "YourSSID"
#define WIFI_PASSWORD "YourPassword"
#define CAT_PORT      60000   // Dummy TS-890S
#define BRIDGE_PORT   60001   // ser2net bridge
#define WEB_PORT      80
#define BRIDGE_BAUD   9600    // Match baud rate of the radio on USB-A

// ── Display ───────────────────────────────────────────────────────────────────
GigaDisplay_GFX        display;
Arduino_GigaDisplayTouch touch;

static const int DW = 800;
static const int DH = 480;

#define C_BG      0x18C3
#define C_PANEL   0x2104
#define C_BORDER  0x294A
#define C_GREEN   0x0666
#define C_WHITE   0xFFFF
#define C_MUTED   0x8410
#define C_AMBER   0xFC40
#define C_ACTIVE  0x0329
#define C_RED     0xF800
#define C_BLUE    0x001F

// ── Band table ────────────────────────────────────────────────────────────────
struct Band { const char* name; long long freq_hz; uint8_t mode; };

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
static const int HDR_H = 100;
static const int COLS  = 4;
static const int ROWS  = 3;
static const int PAD   = 6;
static const int BTN_W = (DW - PAD * (COLS + 1)) / COLS;
static const int BTN_H = (DH - HDR_H - PAD * (ROWS + 1)) / ROWS;

struct Rect { int x, y, w, h; };
Rect btnRects[NUM_BANDS];

// ── Servers ───────────────────────────────────────────────────────────────────
WiFiServer catServer(CAT_PORT);
WiFiServer bridgeServer(BRIDGE_PORT);
WiFiServer webServer(WEB_PORT);

// ── USB bridge state ──────────────────────────────────────────────────────────
USBHostSerial usbSerial;
WiFiClient    bridgeClient;
bool          usbReady       = false;
bool          bridgeActive   = false;

// ── Build Kenwood IF; response ────────────────────────────────────────────────
String buildIF() {
  char buf[48];
  snprintf(buf, sizeof(buf),
    "IF%011lld     +0000000000%d0000000;",
    BANDS[currentBand].freq_hz,
    (int)BANDS[currentBand].mode);
  return String(buf);
}

// ── Display: header ───────────────────────────────────────────────────────────
void drawHeader() {
  display.fillRect(0, 0, DW, HDR_H, C_BG);
  display.drawFastHLine(0, HDR_H - 1, DW, C_BORDER);

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

  display.setTextColor(C_WHITE);
  display.setTextSize(3);
  display.setCursor(16, 64);
  display.print(BANDS[currentBand].name);

  display.setTextColor(C_AMBER);
  display.setCursor(110, 64);
  uint8_t m = BANDS[currentBand].mode;
  display.print((m <= 9) ? MODE_STR[m] : "?");

  // CAT status dot
  display.fillCircle(DW - 24, 30, 8, C_GREEN);
  display.setTextColor(C_MUTED);
  display.setTextSize(1);
  display.setCursor(DW - 100, 24);
  display.print("CAT :60000");

  // Bridge status dot
  uint16_t bridgeCol = bridgeActive ? C_GREEN : (usbReady ? C_AMBER : C_RED);
  display.fillCircle(DW - 24, 68, 8, bridgeCol);
  display.setTextColor(C_MUTED);
  display.setCursor(DW - 100, 62);
  display.print("USB :60001");
}

// ── Display: band button ──────────────────────────────────────────────────────
void drawBtn(int i) {
  if (i >= NUM_BANDS) return;
  const Rect& r = btnRects[i];
  bool active = (i == currentBand);

  display.fillRoundRect(r.x, r.y, r.w, r.h, 8, active ? C_ACTIVE : C_PANEL);
  display.drawRoundRect(r.x, r.y, r.w, r.h, 8, active ? C_GREEN  : C_BORDER);

  int tsize = 3, cw = 18, ch = 24;
  int len = strlen(BANDS[i].name);
  int tx  = r.x + (r.w - len * cw) / 2;
  int ty  = r.y + (r.h - ch) / 2;
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

// ── Touch mapping ─────────────────────────────────────────────────────────────
void remapTouch(int raw_x, int raw_y, int& lx, int& ly) {
  lx = raw_y;
  ly = 480 - raw_x;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  display.begin();
  display.setRotation(1);
  display.fillScreen(C_BG);
  display.setTextColor(C_MUTED);
  display.setTextSize(2);
  display.setCursor(20, 20);
  display.print("Connecting to WiFi...");

  touch.begin();

  for (int i = 0; i < NUM_BANDS; i++) {
    btnRects[i] = {
      PAD + (i % COLS) * (BTN_W + PAD),
      HDR_H + PAD + (i / COLS) * (BTN_H + PAD),
      BTN_W, BTN_H
    };
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.fillScreen(C_BG);
    display.setTextColor(C_RED);
    display.setTextSize(3);
    display.setCursor(20, 20);
    display.print("WiFi failed!");
    while (true) delay(1000);
  }

  catServer.begin();
  bridgeServer.begin();
  webServer.begin();

  IPAddress ip = WiFi.localIP();
  Serial.print("IP: "); Serial.println(ip);

  display.fillScreen(C_BG);
  display.setTextColor(C_GREEN);
  display.setTextSize(4);
  display.setCursor(20, 20);
  display.print("Dummy TS-890S");
  display.setTextColor(C_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 90);  display.print("IP:     "); display.print(ip);
  display.setCursor(20, 120); display.print("CAT:    port 60000");
  display.setCursor(20, 150); display.print("Bridge: port 60001");
  display.setCursor(20, 180); display.print("Web:    http://"); display.print(ip);
  display.setTextColor(C_AMBER);
  display.setCursor(20, 220); display.print("Plug radio into USB-A for bridge");
  display.setTextColor(C_MUTED);
  display.setCursor(20, 260); display.print("Starting in 3 seconds...");
  delay(3000);

  drawAll();
  Serial.println("Ready.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

  // ── Touch ────────────────────────────────────────────────────────────────
  GDTpoint_t pts[5];
  uint8_t n = touch.getTouchPoints(pts);
  if (n > 0) {
    int lx, ly;
    remapTouch(pts[0].x, pts[0].y, lx, ly);
    for (int i = 0; i < NUM_BANDS; i++) {
      const Rect& r = btnRects[i];
      if (lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h) {
        if (i != currentBand) {
          int prev = currentBand;
          currentBand = i;
          drawBtn(prev);
          drawBtn(i);
          drawHeader();
          Serial.print("Touch: "); Serial.println(BANDS[i].name);
        }
        delay(250);
        break;
      }
    }
  }

  // ── Serial command ────────────────────────────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("band ")) {
      int b = cmd.substring(5).toInt();
      if (b >= 0 && b < NUM_BANDS) {
        int prev = currentBand;
        currentBand = b;
        drawBtn(prev); drawBtn(currentBand); drawHeader();
        Serial.print("Band → "); Serial.println(BANDS[currentBand].name);
      }
    }
  }

  // ── USB host: track connect/disconnect ───────────────────────────────────
  bool nowReady = usbSerial.connected();
  if (nowReady && !usbReady) {
    // USB CDC serial — no begin() needed, baud rate is virtual over USB
    usbReady = true;
    drawHeader();
    Serial.println("USB serial: connected");
  } else if (!nowReady && usbReady) {
    usbReady     = false;
    bridgeActive = false;
    if (bridgeClient && bridgeClient.connected()) bridgeClient.stop();
    drawHeader();
    Serial.println("USB serial: disconnected");
  }

  // ── Bridge server: accept new TCP client ─────────────────────────────────
  if (!bridgeClient || !bridgeClient.connected()) {
    WiFiClient newClient = bridgeServer.available();
    if (newClient) {
      bridgeClient = newClient;
      bridgeActive = usbReady;
      drawHeader();
      Serial.print("Bridge client connected");
      if (!usbReady) Serial.print(" (no USB radio)");
      Serial.println();
    } else {
      bridgeActive = false;
    }
  }

  // ── Bridge relay: bidirectional TCP ↔ USB ────────────────────────────────
  if (bridgeClient && bridgeClient.connected() && usbReady) {
    // TCP → USB serial (buffer-based Mbed Stream write)
    if (bridgeClient.available()) {
      uint8_t txBuf[64];
      int n = 0;
      while (bridgeClient.available() && n < (int)sizeof(txBuf))
        txBuf[n++] = bridgeClient.read();
      if (n > 0) usbSerial.write(txBuf, n);
    }
    // USB serial → TCP (buffer-based Mbed Stream read; returns <=0 if no data)
    uint8_t rxBuf[64];
    ssize_t n = usbSerial.read(rxBuf, sizeof(rxBuf));
    if (n > 0) bridgeClient.write(rxBuf, (size_t)n);
  } else if (bridgeClient && bridgeClient.connected() && !usbReady) {
    while (bridgeClient.available()) bridgeClient.read();
  }

  // ── CAT server (port 60000) — dummy TS-890S ──────────────────────────────
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
        cat.print("ID019;");
      }
    }
    cat.stop();
  }

  // ── Web server (port 80) ─────────────────────────────────────────────────
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
        drawBtn(prev); drawBtn(currentBand); drawHeader();
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
        html += "<a href='/band?n="; html += i;
        html += (i == currentBand) ? "' class='on'>" : "'>";
        html += BANDS[i].name; html += "</a>";
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
