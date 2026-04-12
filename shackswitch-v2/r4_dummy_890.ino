/*
 * r4_dummy_890.ino — Dummy TS-890S for ShackSwitch Kenwood CAT testing
 * Arduino Uno R4 WiFi
 *
 * Listens on TCP port 60000 — responds to Kenwood IF; command with a fake
 * frequency for the currently selected band.
 *
 * Serves a simple web page on port 80 with band buttons to change bands.
 *
 * Usage:
 *   1. Set WIFI_SSID and WIFI_PASSWORD below
 *   2. Flash to R4 WiFi
 *   3. Open Serial Monitor to see the assigned IP address
 *   4. Enter that IP in ShackSwitch Kenwood config for Radio B
 *   5. Use the web page (http://<ip>/) to change bands during testing
 */

#include <WiFiS3.h>

// ── Configuration ────────────────────────────────────────────────────────────
#define WIFI_SSID     "YourSSID"
#define WIFI_PASSWORD "YourPassword"
#define CAT_PORT      60000
#define WEB_PORT      80

// ── Band table ────────────────────────────────────────────────────────────────
// One representative frequency per band, in Hz (11 digits for Kenwood IF format)
struct Band {
  const char* name;
  long long   freq_hz;
  const char* mode_digit;   // 1=LSB 2=USB 3=CW
};

const Band BANDS[] = {
  { "160m",  1825000LL, "1" },  // 1.825 MHz LSB
  { "80m",   3750000LL, "1" },  // 3.750 MHz LSB
  { "60m",   5357000LL, "2" },  // 5.357 MHz USB
  { "40m",   7100000LL, "1" },  // 7.100 MHz LSB
  { "30m",  10125000LL, "3" },  // 10.125 MHz CW
  { "20m",  14225000LL, "2" },  // 14.225 MHz USB
  { "17m",  18130000LL, "2" },  // 18.130 MHz USB
  { "15m",  21280000LL, "2" },  // 21.280 MHz USB
  { "12m",  24950000LL, "2" },  // 24.950 MHz USB
  { "10m",  28500000LL, "2" },  // 28.500 MHz USB
  { "6m",   50125000LL, "2" },  // 50.125 MHz USB
};
const int NUM_BANDS = sizeof(BANDS) / sizeof(BANDS[0]);

int currentBand = 5;  // Default: 20m

WiFiServer catServer(CAT_PORT);
WiFiServer webServer(WEB_PORT);

// ── Build an IF; response for the current band ────────────────────────────────
// Kenwood IF format: IF[11-digit freq][spaces+padding = 17 chars][mode][more fields];
// Total before semicolon: ~37 chars. We pad correctly so ShackSwitch can parse it.
String buildIF() {
  char buf[64];
  long long f = BANDS[currentBand].freq_hz;
  // IF + 11-digit freq + 16 filler chars + mode digit + 7 filler chars + ;
  snprintf(buf, sizeof(buf),
    "IF%011lld     +0000000000%s0000000;",
    f, BANDS[currentBand].mode_digit);
  return String(buf);
}

// ── Web page HTML ─────────────────────────────────────────────────────────────
void sendWebPage(WiFiClient& client) {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Dummy TS-890S</title>"
    "<style>"
    "body{background:#111;color:#e8e8e8;font-family:sans-serif;padding:20px;}"
    "h1{color:#00cc66;font-size:20px;margin-bottom:4px;}"
    "p{color:#888;font-size:13px;margin-bottom:16px;}"
    ".freq{font-size:32px;font-weight:bold;color:#00cc66;margin-bottom:20px;}"
    ".grid{display:flex;flex-wrap:wrap;gap:8px;}"
    ".btn{padding:10px 16px;font-size:15px;background:#222;color:#e8e8e8;"
    "border:1px solid #444;border-radius:4px;cursor:pointer;text-decoration:none;}"
    ".btn.active{background:#1a3329;border-color:#00cc66;color:#00cc66;font-weight:bold;}"
    ".btn:hover{border-color:#00cc66;}"
    "</style></head><body>"
    "<h1>Dummy TS-890S</h1>"
    "<p>Click a band to change the simulated frequency</p>"
    "<div class='freq' id='f'>");

  // Current frequency
  html += String(BANDS[currentBand].freq_hz / 1000000.0, 3);
  html += F(" MHz &nbsp; ");
  html += String(BANDS[currentBand].name);
  html += F("</div><div class='grid'>");

  for (int i = 0; i < NUM_BANDS; i++) {
    html += "<a href='/band?n=";
    html += String(i);
    html += "' class='btn";
    if (i == currentBand) html += " active";
    html += "'>";
    html += String(BANDS[i].name);
    html += "</a>";
  }

  html += F("</div></body></html>");

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(html.length());
  client.println();
  client.print(html);
}

void sendRedirect(WiFiClient& client) {
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed — check SSID/password");
    while (true) delay(1000);
  }

  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("CAT server:  ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(CAT_PORT);
  Serial.print("Web page:    http://");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.println("Default band: 20m (14.225 MHz USB)");
  Serial.println("Send 'band N' on Serial to change (0=160m ... 10=6m)");

  catServer.begin();
  webServer.begin();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // Serial command: "band N" to change band
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("band ")) {
      int n = cmd.substring(5).toInt();
      if (n >= 0 && n < NUM_BANDS) {
        currentBand = n;
        Serial.print("Band → ");
        Serial.print(BANDS[currentBand].name);
        Serial.print("  (");
        Serial.print(BANDS[currentBand].freq_hz);
        Serial.println(" Hz)");
      }
    }
  }

  // CAT server (port 60000) — handle one command per loop
  WiFiClient catClient = catServer.available();
  if (catClient) {
    String req = "";
    unsigned long t = millis();
    while (catClient.connected() && millis() - t < 2000) {
      if (catClient.available()) {
        char c = catClient.read();
        req += c;
        if (c == ';') break;  // End of Kenwood command
      }
    }
    req.trim();
    Serial.print("CAT RX: ");
    Serial.println(req);

    if (req.indexOf("IF") >= 0) {
      String resp = buildIF();
      catClient.print(resp);
      Serial.print("CAT TX: ");
      Serial.println(resp);
    } else if (req.startsWith("ID")) {
      catClient.print("ID019;");   // TS-890S ID
    } else if (req.startsWith("FA")) {
      // FA; — VFO A frequency query
      char buf[20];
      snprintf(buf, sizeof(buf), "FA%011lld;", BANDS[currentBand].freq_hz);
      catClient.print(buf);
    }
    catClient.stop();
  }

  // Web server (port 80)
  WiFiClient webClient = webServer.available();
  if (webClient) {
    String req = "";
    unsigned long t = millis();
    while (webClient.connected() && millis() - t < 1000) {
      if (webClient.available()) {
        char c = webClient.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }

    // Parse the request line
    int lineEnd = req.indexOf('\r');
    String reqLine = (lineEnd > 0) ? req.substring(0, lineEnd) : req;

    if (reqLine.indexOf("GET /band?n=") >= 0) {
      int nPos = reqLine.indexOf("n=") + 2;
      int nEnd = reqLine.indexOf(' ', nPos);
      int n    = reqLine.substring(nPos, nEnd).toInt();
      if (n >= 0 && n < NUM_BANDS) {
        currentBand = n;
        Serial.print("Web: band → ");
        Serial.println(BANDS[currentBand].name);
      }
      sendRedirect(webClient);
    } else {
      sendWebPage(webClient);
    }

    delay(5);
    webClient.stop();
  }
}
