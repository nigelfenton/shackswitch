/* 
 * VERSION 1.5 - G0JKN Antenna Switcher 
 * Integrated State Sync, Background WiFi Manager, and Factory Reset
 * Updated for dual-state image buttons (b1-b4) on Page 0
 * Antenna name labels: t3=b1, t4=b2, t5=b3, t6=b4
 * v1.5: Added TCP control protocol (port 9008), UDP discovery beacon,
 *       Flex-6700 band tracking, and SO2R interlock logic
 */

#include "WiFiS3.h"
#include "EasyNextionLibrary.h"
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
#include "RTC.h"
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ── Config struct ─────────────────────────────────────────────
struct RelayConfig { 
  char names[4][26]; 
  char wifiSSID[33];      
  char wifiPass[64];
  uint32_t configMagic;   
};

RelayConfig myConfig;
const uint32_t CONFIG_VERSION = 0xDEADBEEE;

// ── Hardware ──────────────────────────────────────────────────
const int relayPins[] = {2, 3, 4, 5};
const int RELAY_COUNT = 4;

// Ground symbol for 12x8 LED Matrix
const uint32_t ground_hex[] = {
  0x4004004,
  0x7fc0001,
  0xf00000e0,
  66
};

ArduinoLEDMatrix matrix;
EasyNex myNex(Serial1);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -14400, 60000);

// ── Nextion colours ───────────────────────────────────────────
const uint32_t NEXTION_GREEN  = 1024;
const uint32_t NEXTION_RED    = 63488;
const uint32_t NEXTION_ORANGE = 65504;  // SO2R inhibit warning

// ── Web server (port 80) ──────────────────────────────────────
WiFiServer server(80);

// ── TCP control server (port 9008) ───────────────────────────
WiFiServer tcpServer(9008);
WiFiClient tcpClient;

// ── UDP discovery beacon ──────────────────────────────────────
WiFiUDP discoveryUDP;
const uint16_t DISCOVERY_PORT = 9008;

// ── Device identity ───────────────────────────────────────────
const char* DEVICE_NAME    = "ShackSwitch";
const char* DEVICE_SERIAL  = "SS-001";
const char* DEVICE_VERSION = "1.5.0";

// ── Band definitions ──────────────────────────────────────────
struct BandInfo {
  int    id;
  const char* name;
  double freqStartMhz;
  double freqStopMhz;
};

const BandInfo BANDS[] = {
  { 1, "160m",  1.800,  2.000 },
  { 2,  "80m",  3.500,  4.000 },
  { 3,  "60m",  5.330,  5.407 },  // covers all channels + WRC-15
  { 4,  "40m",  7.000,  7.300 },
  { 5,  "30m", 10.100, 10.160 },  // extended
  { 6,  "20m", 14.000, 14.350 },
  { 7,  "17m", 18.068, 18.168 },
  { 8,  "15m", 21.000, 21.450 },
  { 9,  "12m", 24.890, 24.990 },
  {10,  "10m", 28.000, 29.700 },
  {11,   "6m", 50.000, 54.000 },
};
const int BAND_COUNT = sizeof(BANDS) / sizeof(BANDS[0]);

// ── Port state (Radio A = port 1, Radio B = port 2) ──────────
struct PortState {
  int  portId;
  bool autoMode;
  int  band;          // current band id (0 = unknown)
  int  rxAntenna;     // 1-based, 0 = none
  int  txAntenna;
  bool transmitting;
  bool inhibited;
  char inhibitReason[64];
};

PortState portA = {1, true, 0, 0, 0, false, false, ""};
PortState portB = {2, true, 0, 0, 0, false, false, ""};

// ── TCP command state ─────────────────────────────────────────
char tcpLineBuffer[256];
int  tcpLinePos = 0;

// ── Timers ────────────────────────────────────────────────────
unsigned long lastRSSIUpdate    = 0;
unsigned long lastClockUpdate   = 0;
unsigned long lastWiFiCheck     = 0;
unsigned long lastBeacon        = 0;
unsigned long lastKeepalive     = 0;
unsigned long resetTimer        = 0;

const unsigned long BEACON_INTERVAL    = 5000;
const unsigned long KEEPALIVE_INTERVAL = 30000;

// ── UI State ──────────────────────────────────────────────────
int  currentRSSI      = 0;
bool onMonitorPage    = false;
bool onConfigPage     = false;
bool resetConfirmed   = false;
int  currentActiveRelay = 0;

// ═════════════════════════════════════════════════════════════
//  BAND HELPERS
// ═════════════════════════════════════════════════════════════
int bandForFreq(double freqMhz) {
  for (int i = 0; i < BAND_COUNT; i++) {
    if (freqMhz >= BANDS[i].freqStartMhz && freqMhz <= BANDS[i].freqStopMhz)
      return BANDS[i].id;
  }
  return 0;
}

const char* bandName(int bandId) {
  for (int i = 0; i < BAND_COUNT; i++) {
    if (BANDS[i].id == bandId) return BANDS[i].name;
  }
  return "---";
}

// ═════════════════════════════════════════════════════════════
//  RELAY & DISPLAY
// ═════════════════════════════════════════════════════════════
void updateMatrix(int num) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  if(num > 0) { matrix.print("R"); matrix.print(num); } 
  else { matrix.print("GN "); }
  matrix.endText();
  matrix.endDraw();
}

void displayGroundSymbol() {
  matrix.loadFrame(ground_hex);
}

void syncButtonStates() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    int val = (digitalRead(relayPins[i]) == HIGH) ? 1 : 0;
    myNex.writeNum("b" + String(i + 1) + ".val", val);
  }
}

void syncAntennaNames() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    myNex.writeStr("t" + String(i + 3) + ".txt", myConfig.names[i]);
  }
}

void updateAntennaStatus() {
  bool anyActive  = false;
  int  activeIndex = -1;
  for (int i = 0; i < RELAY_COUNT; i++) {
    if (digitalRead(relayPins[i]) == HIGH) { anyActive = true; activeIndex = i; break; }
  }
  if (anyActive) {
    myNex.writeStr("tState.txt", "ANT Active");
    myNex.writeStr("tState.pco", "63488");
    updateMatrix(activeIndex + 1);
  } else {
    myNex.writeStr("tState.txt", "ANT Grounded");
    myNex.writeStr("tState.pco", "1024");
    updateMatrix(0);
    displayGroundSymbol();
  }
}

/*
 * FUNCTION: controlRelay
 * ----------------------
 * Sets a relay HIGH or LOW. Only one relay active at a time.
 * Syncs Nextion dual-state buttons and LED matrix.
 * Called by: web server, TCP protocol, SO2R band-change logic.
 */
void controlRelay(int targetIndex, bool state) {
  int idx = targetIndex - 1;
  if (idx < 0 || idx >= RELAY_COUNT) return;

  if (state == HIGH) {
    currentActiveRelay = targetIndex;
    for (int i = 0; i < RELAY_COUNT; i++) {
      digitalWrite(relayPins[i], (i == idx) ? HIGH : LOW);
    }
  } else {
    digitalWrite(relayPins[idx], LOW);
    if (currentActiveRelay == targetIndex) currentActiveRelay = 0;
  }
  syncButtonStates();
  updateAntennaStatus();
  updateNextionBandDisplay();
}

/*
 * FUNCTION: updateNextionBandDisplay
 * -----------------------------------
 * Pushes current band and SO2R status to Nextion.
 * Uses tBandA, tBandB, tSO2R text components.
 * Add these components to your Nextion HMI page 0.
 */
void updateNextionBandDisplay() {
  myNex.writeStr("tBandA.txt", bandName(portA.band));
  myNex.writeStr("tBandB.txt", bandName(portB.band));

  if (portA.inhibited || portB.inhibited) {
    myNex.writeStr("tSO2R.txt", "INHIBIT");
    myNex.writeNum("tSO2R.pco", NEXTION_ORANGE);
  } else {
    myNex.writeStr("tSO2R.txt", "SO2R OK");
    myNex.writeNum("tSO2R.pco", NEXTION_GREEN);
  }
}

// ═════════════════════════════════════════════════════════════
//  SO2R INTERLOCK
// ═════════════════════════════════════════════════════════════
void evaluateInterlock() {
  portA.inhibited = false;
  portB.inhibited = false;
  portA.inhibitReason[0] = '\0';
  portB.inhibitReason[0] = '\0';

  if (!portA.transmitting || !portB.transmitting) return;

  // Same antenna conflict
  if (portA.txAntenna != 0 && portA.txAntenna == portB.txAntenna) {
    portB.inhibited = true;
    snprintf(portB.inhibitReason, sizeof(portB.inhibitReason),
      "same antenna as Radio A (ant %d)", portB.txAntenna);
    Serial.println(F("[INTERLOCK] Same antenna — Radio B inhibited"));
    return;
  }

  // Same band conflict
  if (portA.band != 0 && portA.band == portB.band) {
    portB.inhibited = true;
    snprintf(portB.inhibitReason, sizeof(portB.inhibitReason),
      "same band as Radio A (%s)", bandName(portB.band));
    Serial.println(F("[INTERLOCK] Same band — Radio B inhibited"));
  }
}

// ═════════════════════════════════════════════════════════════
//  TCP PROTOCOL
// ═════════════════════════════════════════════════════════════
void sendTcpResponse(int seq, int code, const char* body) {
  if (!tcpClient || !tcpClient.connected()) return;
  char line[256];
  snprintf(line, sizeof(line), "R%d|%02X|%s", seq, code, body);
  tcpClient.println(line);
}

void pushPortStatus(PortState* p) {
  if (!tcpClient || !tcpClient.connected()) return;
  char line[192];
  snprintf(line, sizeof(line),
    "S0|port id=%d auto=%d band=%d rxant=%d txant=%d tx=%d inhibited=%d reason=%s",
    p->portId, p->autoMode ? 1 : 0, p->band,
    p->rxAntenna, p->txAntenna,
    p->transmitting ? 1 : 0,
    p->inhibited ? 1 : 0,
    p->inhibitReason
  );
  tcpClient.println(line);
}

PortState* portById(int id) {
  if (id == 1) return &portA;
  if (id == 2) return &portB;
  return nullptr;
}

// Simple key=value parser — finds value for a given key in a string
// e.g. parseParam("rxant=3 txant=3", "rxant") returns 3
int parseParam(const char* str, const char* key) {
  char* found = strstr(str, key);
  if (!found) return -1;
  found += strlen(key);
  if (*found == '=') found++;
  return atoi(found);
}

void processTcpCommand(const char* line) {
  if (line[0] != 'C') return;

  int seq = 0;
  int i = 1;
  while (line[i] && line[i] != '|') { seq = seq * 10 + (line[i] - '0'); i++; }
  if (line[i] != '|') return;
  const char* cmd = &line[i + 1];

  Serial.print(F("[TCP] seq=")); Serial.print(seq);
  Serial.print(F(" cmd=")); Serial.println(cmd);

  if (strncmp(cmd, "ping", 4) == 0) {
    sendTcpResponse(seq, 0, "");

  } else if (strncmp(cmd, "antenna list", 12) == 0) {
    for (int a = 0; a < RELAY_COUNT; a++) {
      char body[96];
      snprintf(body, sizeof(body),
        "id=%d name=%s txBandMask=FFFF rxBandMask=FFFF rxOnly=0", a+1, myConfig.names[a]);
      sendTcpResponse(seq, 0, body);
    }
    sendTcpResponse(seq, 0, "");

  } else if (strncmp(cmd, "band list", 9) == 0) {
    for (int b = 0; b < BAND_COUNT; b++) {
      char body[64];
      snprintf(body, sizeof(body), "id=%d name=%s freqStart=%.3f freqStop=%.3f",
        BANDS[b].id, BANDS[b].name, BANDS[b].freqStartMhz, BANDS[b].freqStopMhz);
      sendTcpResponse(seq, 0, body);
    }
    sendTcpResponse(seq, 0, "");

  } else if (strncmp(cmd, "port get ", 9) == 0) {
    int portId = atoi(&cmd[9]);
    PortState* p = portById(portId);
    if (!p) { sendTcpResponse(seq, 0x404, "invalid port"); return; }
    char body[128];
    snprintf(body, sizeof(body),
      "id=%d auto=%d band=%d rxant=%d txant=%d tx=%d inhibited=%d reason=%s",
      p->portId, p->autoMode?1:0, p->band, p->rxAntenna, p->txAntenna,
      p->transmitting?1:0, p->inhibited?1:0, p->inhibitReason);
    sendTcpResponse(seq, 0, body);

  } else if (strncmp(cmd, "port set ", 9) == 0) {
    // Format: "port set <portId> rxant=<n> txant=<n> auto=<0|1> band=<n>"
    const char* params = &cmd[9];
    int portId  = atoi(params);
    int rxAnt   = parseParam(params, "rxant");
    int txAnt   = parseParam(params, "txant");
    int autoMode = parseParam(params, "auto");
    int band    = parseParam(params, "band");

    PortState* p = portById(portId);
    if (!p) { sendTcpResponse(seq, 0x404, "invalid port"); return; }

    if (autoMode >= 0) p->autoMode = (autoMode == 1);
    if (band > 0)     p->band = band;

    // Apply antenna selection — only port A drives relays on V1 shield
    // Port B support requires V2/V3 shield (second relay bank)
    if (rxAnt > 0 && rxAnt <= RELAY_COUNT) {
      p->rxAntenna = rxAnt;
      if (portId == 1) {
        // Radio A controls the relay bank
        controlRelay(rxAnt, HIGH);
        Serial.print(F("[RELAY] Port A → Antenna ")); Serial.println(rxAnt);
      }
      // TODO: Port B relay bank when V2/V3 shield fitted
    }
    if (txAnt > 0 && txAnt <= RELAY_COUNT) p->txAntenna = txAnt;

    evaluateInterlock();
    sendTcpResponse(seq, 0, "ok");
    pushPortStatus(p);
    updateNextionBandDisplay();

  } else if (strncmp(cmd, "sub port all", 12) == 0) {
    sendTcpResponse(seq, 0, "subscribed");

  } else if (strncmp(cmd, "interlock set ", 14) == 0) {
    const char* params = &cmd[14];
    bool tx   = false;
    int  band = parseParam(params, "band");

    if (strstr(params, "radioA=")) {
      tx = parseParam(params, "radioA") == 1;
      portA.transmitting = tx;
      if (band > 0) portA.band = band;
    }
    if (strstr(params, "radioB=")) {
      tx = parseParam(params, "radioB") == 1;
      portB.transmitting = tx;
      if (band > 0) portB.band = band;
    }

    evaluateInterlock();
    sendTcpResponse(seq, 0, "ok");
    pushPortStatus(&portA);
    pushPortStatus(&portB);
    updateNextionBandDisplay();

  } else {
    sendTcpResponse(seq, 0x500, "unknown command");
  }
}

void handleTCP() {
  // Accept new client
  if (!tcpClient || !tcpClient.connected()) {
    tcpClient = tcpServer.available();
    if (tcpClient) {
      Serial.println(F("[TCP] Client connected"));
      tcpLinePos = 0;
      tcpClient.println(F("S0|device name=ShackSwitch ready=1"));
    }
    return;
  }

  // Read lines
  while (tcpClient.available()) {
    char c = tcpClient.read();
    if (c == '\n') {
      tcpLineBuffer[tcpLinePos] = '\0';
      if (tcpLinePos > 0 && tcpLineBuffer[tcpLinePos-1] == '\r')
        tcpLineBuffer[--tcpLinePos] = '\0';
      if (tcpLinePos > 0) processTcpCommand(tcpLineBuffer);
      tcpLinePos = 0;
    } else if (tcpLinePos < (int)sizeof(tcpLineBuffer) - 1) {
      tcpLineBuffer[tcpLinePos++] = c;
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  UDP DISCOVERY BEACON
// ═════════════════════════════════════════════════════════════
void handleDiscoveryBeacon() {
  unsigned long now = millis();
  if (now - lastBeacon < BEACON_INTERVAL) return;
  lastBeacon = now;

  if (WiFi.status() != WL_CONNECTED) return;

  char beacon[160];
  snprintf(beacon, sizeof(beacon),
    "SS name=%s serial=%s version=%s ip=%s port=9008 ant=%d radio=2",
    DEVICE_NAME, DEVICE_SERIAL, DEVICE_VERSION,
    WiFi.localIP().toString().c_str(),
    RELAY_COUNT
  );

  IPAddress broadcast = WiFi.localIP();
  broadcast[3] = 255;
  discoveryUDP.beginPacket(broadcast, DISCOVERY_PORT);
  discoveryUDP.write((uint8_t*)beacon, strlen(beacon));
  discoveryUDP.endPacket();
}

// ═════════════════════════════════════════════════════════════
//  WIFI & CONFIG
// ═════════════════════════════════════════════════════════════
void loadConfig() {
  EEPROM.get(0, myConfig);
  if (myConfig.configMagic != CONFIG_VERSION) {
    Serial.println("EEPROM Empty. Loading Defaults...");
    for (int i = 0; i < RELAY_COUNT; i++) {
      String dName = "Relay " + String(i + 1);
      dName.toCharArray(myConfig.names[i], 26);
    }
    strncpy(myConfig.wifiSSID, "tinkerbell", 33);
    strncpy(myConfig.wifiPass, "disneybell", 64);
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
  }
}

void connectToWiFi() {
  WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
  Serial.print("Connecting to: "); Serial.println(myConfig.wifiSSID);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    delay(500);
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection timed out.");
  }
}

void syncTime() {
  timeClient.begin();
  if(timeClient.update()) {
    RTCTime currentTime(timeClient.getEpochTime());
    RTC.setTime(currentTime);
    Serial.println("NTP Synced.");
  }
}

void updateStationMonitor() {
  if (WiFi.status() == WL_CONNECTED) {
    currentRSSI = WiFi.RSSI();
    int signalQuality = map(currentRSSI, -100, -40, 0, 100);
    signalQuality = constrain(signalQuality, 0, 100);
    myNex.writeStr("tRSSI.txt", String(currentRSSI) + " dBm");
    myNex.writeNum("nSignal.val", signalQuality);
    myNex.writeStr("b0.txt", WiFi.localIP().toString());
    if (currentRSSI > -60)      myNex.writeNum("nSignal.pco", NEXTION_GREEN);
    else if (currentRSSI > -80) myNex.writeNum("nSignal.pco", NEXTION_RED);
  }
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGES (unchanged from v1.4)
// ═════════════════════════════════════════════════════════════
void showMainPage(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n<!DOCTYPE HTML><html>");
  c.println("<head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  c.println("<style>");
  c.println("body{font-family:Arial;text-align:center;background:#1a1a1a;color:white;}");
  c.println(".card{background:#333;margin:10px auto;padding:15px;width:90%;max-width:350px;border-radius:10px;transition:background 0.3s;}");
  c.println(".card.active{background:#1a472a;border:2px solid #28a745;}");
  c.println(".btn{display:block;padding:15px;margin:10px 0;color:#fff;text-decoration:none;border-radius:8px;font-weight:bold;}");
  c.println(".on{background:#28a745;} .off{background:#dc3545;}");
  c.println("</style></head><body>");
  c.println("<h1>G0JKN 1.5 ANT SWITCH</h1>");
  for (int i = 0; i < RELAY_COUNT; i++) {
    bool st = digitalRead(relayPins[i]);
    String cardClass = st ? " active" : "";
    c.print("<div class='card" + cardClass + "' id='card" + String(i+1) + "'>");
    c.print("<strong>" + String(myConfig.names[i]) + "</strong>");
    if(st) c.print("<a href='/" + String(i+1) + "/off' class='btn on' id='btn" + String(i+1) + "'>ACTIVE</a>");
    else   c.print("<a href='/" + String(i+1) + "/on' class='btn off' id='btn" + String(i+1) + "'>GROUNDED</a>");
    c.print("</div>");
  }
  // Band status panel
  c.println("<div style='background:#1a2a3a;margin:10px auto;padding:10px;width:90%;max-width:350px;border-radius:10px;font-size:13px;'>");
  c.println("<strong>Radio A:</strong> " + String(bandName(portA.band)) + " &nbsp; <strong>Radio B:</strong> " + String(bandName(portB.band)));
  c.println("<br><strong>SO2R:</strong> " + String((portA.inhibited || portB.inhibited) ? "⚠ INHIBIT" : "OK"));
  c.println("</div>");
  c.println("<br><a href='/settings' style='color:#666;'>[ SETTINGS ]</a>");
  c.println("<script>");
  c.println("function updateStatus(){");
  c.println(" fetch('/status').then(r=>r.json()).then(d=>{");
  c.println(" for(let i=1;i<=4;i++){");
  c.println("  const on=d['r'+i]===1;");
  c.println("  const card=document.getElementById('card'+i);");
  c.println("  const btn=document.getElementById('btn'+i);");
  c.println("  if(on){card.className='card active';btn.className='btn on';btn.textContent='ACTIVE';btn.href='/'+i+'/off';}");
  c.println("  else{card.className='card';btn.className='btn off';btn.textContent='GROUNDED';btn.href='/'+i+'/on';}");
  c.println(" }");
  c.println(" }).catch(()=>{});");
  c.println("}");
  c.println("setInterval(updateStatus,5000);");
  c.println("</script>");
  c.println("</body></html>");
}

void showSettingsPage(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n<!DOCTYPE HTML><html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head><body style='font-family:Arial;padding:20px;'>");
  c.println("<h2>Station Config</h2>");
  for (int i = 0; i < RELAY_COUNT; i++) {
    c.print("<div style='margin-bottom:20px;'><strong>" + String(myConfig.names[i]) + "</strong>");
    c.print("<form action='/rename'><input type='hidden' name='id' value='"+String(i+1)+"'>");
    c.print("<input type='text' name='name' maxlength='25'><button type='submit'>Save</button></form></div>");
  }
  c.println("<a href='/'>Back</a></body></html>");
}

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  Serial.println(F("G0JKN ShackSwitch v1.5 starting..."));

  matrix.begin();
  myNex.begin(9600);
  loadConfig();
  RTC.begin();

  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  connectToWiFi();

  // Start all servers
  server.begin();
  tcpServer.begin();
  discoveryUDP.begin(DISCOVERY_PORT);

  syncTime();

  myNex.writeStr("page 0");
  syncButtonStates();
  syncAntennaNames();
  myNex.writeStr("t1.txt", WiFi.localIP().toString());
  updateNextionBandDisplay();

  Serial.print(F("Web server on port 80, TCP server on port 9008, IP: "));
  Serial.println(WiFi.localIP());
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════
void loop() {
  myNex.NextionListen();

  // ── TCP control protocol ──────────────────────────────────
  handleTCP();

  // ── UDP discovery beacon ──────────────────────────────────
  handleDiscoveryBeacon();

  // ── TCP keepalive ─────────────────────────────────────────
  if (tcpClient && tcpClient.connected()) {
    if (millis() - lastKeepalive > KEEPALIVE_INTERVAL) {
      lastKeepalive = millis();
      tcpClient.println(F("S0|ping"));
    }
  }

  // ── RSSI update ───────────────────────────────────────────
  if (millis() - lastRSSIUpdate > 10000) {
    if (onMonitorPage || WiFi.status() == WL_CONNECTED) updateStationMonitor();
    lastRSSIUpdate = millis();
  }

  // ── Reset safety timeout ──────────────────────────────────
  if (resetConfirmed && (millis() - resetTimer > 5000)) {
    resetConfirmed = false;
    myNex.writeStr("tStatus.txt", "Reset Canceled");
  }

  // ── WiFi reconnect ────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED && !onConfigPage) {
    if (millis() - lastWiFiCheck > 30000) {
      WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
      lastWiFiCheck = millis();
    }
    myNex.writeStr("t1.txt", "N O T   C O N N E C T E D");
  }

  // ── RTC clock update ──────────────────────────────────────
  if (millis() - lastClockUpdate > 25000) {
    RTCTime now;
    if (RTC.getTime(now)) {
      char buf[16];
      sprintf(buf, "%02d:%02d", (int)now.getHour(), (int)now.getMinutes());
      myNex.writeStr("tClock.txt", buf);
      if (now.getHour() == 0 && now.getMinutes() == 0 && now.getSeconds() < 10) syncTime();
    }
    lastClockUpdate = millis();
  }

  // ── Web server ────────────────────────────────────────────
  WiFiClient webClient = server.available();
  if (webClient) {
    String request = webClient.readStringUntil('\r');
    webClient.flush();

    if (request.indexOf("GET /status") != -1) {
      String json = "{";
      for (int i = 0; i < RELAY_COUNT; i++) {
        json += "\"r" + String(i+1) + "\":" + String(digitalRead(relayPins[i]));
        if (i < RELAY_COUNT - 1) json += ",";
      }
      json += ",\"bandA\":\"" + String(bandName(portA.band)) + "\"";
      json += ",\"bandB\":\"" + String(bandName(portB.band)) + "\"";
      json += ",\"so2r\":" + String((portA.inhibited || portB.inhibited) ? "1" : "0");
      json += "}";
      webClient.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + json);
    }
    else if (request.indexOf("GET /settings") != -1) { showSettingsPage(webClient); }
    else if (request.indexOf("GET /rename") != -1) {
      int idPos   = request.indexOf("id=") + 3;
      int rId     = request.substring(idPos, idPos + 1).toInt();
      int namePos = request.indexOf("name=") + 5;
      int endPos  = request.indexOf(" ", namePos);
      String nN   = request.substring(namePos, endPos);
      nN.replace("+", " "); nN.replace("%20", " ");
      if (rId >= 1 && rId <= RELAY_COUNT) {
        nN.toCharArray(myConfig.names[rId-1], 26);
        myConfig.configMagic = CONFIG_VERSION;
        EEPROM.put(0, myConfig);
        myNex.writeStr("t" + String(rId + 2) + ".txt", nN);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /settings\r\n\r\n");
    }
    else if (request.indexOf("/on") != -1 || request.indexOf("/off") != -1) {
      for (int i = 1; i <= RELAY_COUNT; i++) {
        if (request.indexOf("/" + String(i) + "/on") != -1)  controlRelay(i, HIGH);
        if (request.indexOf("/" + String(i) + "/off") != -1) controlRelay(i, LOW);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
    }
    else { showMainPage(webClient); }
    webClient.stop();
  }
}
