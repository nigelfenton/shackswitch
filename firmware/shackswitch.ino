/*
 * ============================================================
 *  G0JKN ShackSwitch — Firmware v1.5
 *  Nigel Fenton, G0JKN
 *  https://github.com/nigelfenton/shackswitch
 *
 *  Hardware:
 *    Arduino Uno R4 WiFi
 *    Nextion NX4832T035 3.5" touchscreen (via Serial1, EasyNextionLibrary)
 *    4x relay module on D2-D5 (via transistor driver shield)
 *    5x SO239 RF connectors (1 radio input, 4 antenna outputs)
 *    12x8 onboard LED matrix
 *    DS3231 RTC (via RTC library) + NTP sync on connect
 *
 *  Features (v1.5):
 *    - HTTP web server on port 80 (relay control, antenna rename, band update, status)
 *    - TCP control protocol on port 9008 (AetherSDR / Node-RED integration)
 *    - UDP discovery beacon on port 9008 (auto-discovery by network clients)
 *    - FlexRadio band tracking for two input ports (Slice A / Slice B)
 *    - SO2R interlock — inhibits second port on same-band or same-antenna TX conflict
 *    - Nextion HMI: tBandA, tBandB, tSO2R band/status display
 *    - EEPROM config persistence (antenna names, WiFi credentials)
 *    - NTP time sync to onboard RTC, displayed on Nextion
 *    - Background WiFi reconnect
 *    - Factory reset with double-tap confirmation and 5-second safety timeout
 *
 *  Nextion HMI components:
 *    Page 0 (4-port):  bA1-bA4, t3-t6
 *    Page 1 (2x6):     bA1-bA6, t3-t8, bB1-bB6
 *    Page 2 (2x8):     bA1-bA8, t3-t10, bB1-bB8
 *    All pages:        tBandA, tBandB, tSO2R, tState, tClock, t1
 *
 *  Trigger number map:
 *    01-08  Input 1 bA1-bA8
 *    11-18  Input 2 bB1-bB8 (hex 11=trigger17 through 18=trigger24)
 *    21-26  Control functions (hex 21=trigger33 through 26=trigger38)
 *
 *  REST API endpoints:
 *    GET /status                          — relay states, band, SO2R as JSON
 *    GET /[n]/on                          — activate relay n (1-4)
 *    GET /[n]/off                         — ground relay n
 *    GET /rename?id=[n]&name=[name]       — rename antenna port n
 *    GET /setband?input=[1|2]&band=[name] — set band for Input 1 or 2 (e.g. "40m")
 *    GET /settings                        — settings web page
 *
 *  TCP protocol (port 9008):
 *    Commands: C[seq]|<command>
 *    Responses: R[seq]|<code>|<body>  or  S0|<event>
 *    Commands: ping, antenna list, band list, port get <n>,
 *              port set <n> rxant=<n> txant=<n> auto=<0|1> band=<n>,
 *              sub port all, interlock set radioA=<0|1> radioB=<0|1> band=<n>
 *
 *  Licence: MIT — see LICENSE
 * ============================================================
 */

#include "WiFiS3.h"
#include "EasyNextionLibrary.h"
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
#include "RTC.h"
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>


// ============================================================
//  CONFIG STRUCT
//  Stored in EEPROM. configMagic acts as a version guard —
//  if the magic value doesn't match CONFIG_VERSION, defaults
//  are loaded and written to EEPROM.
// ============================================================
struct RelayConfig {
  char     names[8][26];   // Antenna names, up to 25 chars + null extended from 4 to 8
  char     wifiSSID[33];
  char     wifiPass[64];
  uint8_t  portMode;      // 0=4port, 1=2x6, 2=2x8   debug port type setting
  uint32_t configMagic;    // Must equal CONFIG_VERSION to be valid
};

RelayConfig myConfig;
const uint32_t CONFIG_VERSION = 0xDEADC001;


// ============================================================
//  HARDWARE
// ============================================================

// Relay output pins — one relay per antenna port
const int relayPins[] = {2, 3, 4, 5};
const int RELAY_COUNT = 8;

// Ground symbol bitmap for the 12x8 LED matrix
const uint32_t ground_hex[] = {
  0x4004004,
  0x7fc0001,
  0xf00000e0,
  66
};

ArduinoLEDMatrix matrix;
EasyNex           myNex(Serial1);   // Nextion on hardware Serial1
WiFiUDP           ntpUDP;
NTPClient         timeClient(ntpUDP, "pool.ntp.org", -14400, 60000);


// ============================================================
//  NEXTION COLOURS
//  Nextion uses 16-bit RGB565 colour values
// ============================================================
const uint32_t NEXTION_GREEN  = 1024;
const uint32_t NEXTION_RED    = 63488;
const uint32_t NEXTION_ORANGE = 65504;   // Used for SO2R inhibit warning


// ============================================================
//  SERVERS
// ============================================================
WiFiServer server(80);          // HTTP web server
WiFiServer tcpServer(9008);     // TCP control protocol
WiFiClient tcpClient;           // Single connected TCP client

WiFiUDP discoveryUDP;
const uint16_t DISCOVERY_PORT = 9008;


// ============================================================
//  DEVICE IDENTITY
//  Used in TCP greeting and UDP discovery beacon
// ============================================================
const char* DEVICE_NAME    = "ShackSwitch";
const char* DEVICE_SERIAL  = "SS-001";
const char* DEVICE_VERSION = "1.5.0";


// ============================================================
//  BAND DEFINITIONS
//  Covers all amateur HF bands 160m through 6m.
//  Frequency ranges are inclusive. bandForFreq() maps a
//  frequency in MHz to a band ID. bandName() maps an ID back
//  to its display string.
// ============================================================
struct BandInfo {
  int         id;
  const char* name;
  double      freqStartMhz;
  double      freqStopMhz;
};

const BandInfo BANDS[] = {
  {  1, "160m",  1.800,  2.000 },
  {  2,  "80m",  3.500,  4.000 },
  {  3,  "60m",  5.330,  5.407 },  // Covers all channels + WRC-15 allocation
  {  4,  "40m",  7.000,  7.300 },
  {  5,  "30m", 10.100, 10.160 },
  {  6,  "20m", 14.000, 14.350 },
  {  7,  "17m", 18.068, 18.168 },
  {  8,  "15m", 21.000, 21.450 },
  {  9,  "12m", 24.890, 24.990 },
  { 10,  "10m", 28.000, 29.700 },
  { 11,   "6m", 50.000, 54.000 },
};
const int BAND_COUNT = sizeof(BANDS) / sizeof(BANDS[0]);


// ============================================================
//  PORT STATE
//  The ShackSwitch models two independent input ports:
//    portA = Input 1 / FlexRadio Slice A
//    portB = Input 2 / FlexRadio Slice B
//  Each port tracks its current band, antenna selection,
//  TX state, and interlock inhibit status independently.
//
//  Note: The FlexRadio can present multiple RX slices
//  simultaneously with TX on one slice only. The SO2R
//  interlock tracks TX state per port. Band changes on
//  RX-only slices do not drive antenna switching — only
//  the TX slice drives relay selection. Multi-RX within a
//  single slice is a known future consideration.
// ============================================================
struct PortState {
  int  portId;
  bool autoMode;
  int  band;           // Current band ID (0 = unknown/no band set)
  int  rxAntenna;      // 1-based antenna index, 0 = none selected
  int  txAntenna;      // 1-based antenna index for TX path
  bool transmitting;
  bool inhibited;
  char inhibitReason[64];
};

PortState portA = {1, true, 0, 0, 0, false, false, ""};
PortState portB = {2, true, 0, 0, 0, false, false, ""};


// ============================================================
//  TCP COMMAND STATE
//  Reads lines from the TCP client incrementally in loop().
//  Lines are terminated by \n (with optional \r stripped).
// ============================================================
char tcpLineBuffer[256];
int  tcpLinePos = 0;


// ============================================================
//  TIMERS
// ============================================================
unsigned long lastRSSIUpdate    = 0;
unsigned long lastClockUpdate   = 0;
unsigned long lastWiFiCheck     = 0;
unsigned long lastBeacon        = 0;
unsigned long lastKeepalive     = 0;
unsigned long resetTimer        = 0;

const unsigned long BEACON_INTERVAL    = 5000;    // UDP beacon every 5 seconds
const unsigned long KEEPALIVE_INTERVAL = 30000;   // TCP keepalive every 30 seconds


// ============================================================
//  UI STATE
// ============================================================
int  currentRSSI       = 0;
bool onMonitorPage     = false;
bool onConfigPage      = false;
bool resetConfirmed    = false;
int  currentActiveRelay = 0;   // 1-based, 0 = all grounded


// ============================================================
//  BAND HELPERS
// ============================================================

/*
 * FUNCTION: bandForFreq
 * ---------------------
 * Returns the band ID for a given frequency in MHz.
 * Returns 0 if the frequency does not fall within any
 * defined amateur band.
 */
int bandForFreq(double freqMhz) {
  for (int i = 0; i < BAND_COUNT; i++) {
    if (freqMhz >= BANDS[i].freqStartMhz && freqMhz <= BANDS[i].freqStopMhz)
      return BANDS[i].id;
  }
  return 0;
}

/*
 * FUNCTION: bandName
 * ------------------
 * Returns the display string for a given band ID (e.g. "40m").
 * Returns "---" for unknown or unset band (ID 0 or not found).
 */
const char* bandName(int bandId) {
  for (int i = 0; i < BAND_COUNT; i++) {
    if (BANDS[i].id == bandId) return BANDS[i].name;
  }
  return "---";
}


// ============================================================
//  RELAY & DISPLAY
// ============================================================

/*
 * FUNCTION: updateMatrix
 * ----------------------
 * Displays the active relay number on the 12x8 LED matrix.
 * Passing num=0 displays the ground symbol instead.
 */
void updateMatrix(int num) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  if (num > 0) { matrix.print("R"); matrix.print(num); }
  else         { matrix.print("GN "); }
  matrix.endText();
  matrix.endDraw();
}

/*
 * FUNCTION: displayGroundSymbol
 * ------------------------------
 * Loads the ground bitmap directly onto the LED matrix.
 * Called when all relays are grounded.
 */
void displayGroundSymbol() {
  matrix.loadFrame(ground_hex);
}

/*
 * FUNCTION: getRowCount
 * ----------------------
 * Returns the number of antenna rows for the current port mode.
 * Used by syncButtonStates() and syncAntennaNames() to know
 * how many components to update on the current Nextion page.
 */
int getRowCount() {
  if (myConfig.portMode == 1) return 6;
  if (myConfig.portMode == 2) return 8;
  return 4;
}

/*
 * FUNCTION: syncButtonStates
 * ---------------------------
 * Pushes current antenna selection state to Nextion bA/bB buttons
 * and antenna name labels for all pages.
 *
 * All three pages now use consistent naming:
 *   bA1-bA8 = Input 1 buttons (green when active)
 *   bB1-bB8 = Input 2 buttons (orange when active)
 *   t3-t10  = Antenna name labels
 *
 * Colours:
 *   33840  = grey   (unselected)
 *   1024   = green  (Input 1 active)
 *   65504  = orange (Input 2 active)
 */
void syncButtonStates() {
  int rows = getRowCount();
  for (int i = 1; i <= rows; i++) {

    // Input 1 button — green when active, grey when not
    bool aActive = (portA.rxAntenna == i);
    uint32_t aColour = aActive ? NEXTION_GREEN : 33840;
    myNex.writeNum("bA" + String(i) + ".bco",  aColour);
    myNex.writeNum("bA" + String(i) + ".bco2", aColour);
    myNex.writeNum("bA" + String(i) + ".pco",  65535);   // white text always
    myNex.writeNum("bA" + String(i) + ".pco2", 65535);

    // Input 2 button — orange when active, grey when not
    bool bActive = (portB.rxAntenna == i);
    uint32_t bColour = bActive ? NEXTION_ORANGE : 33840;
    myNex.writeNum("bB" + String(i) + ".bco",  bColour);
    myNex.writeNum("bB" + String(i) + ".bco2", bColour);
    myNex.writeNum("bB" + String(i) + ".pco",  65535);
    myNex.writeNum("bB" + String(i) + ".pco2", 65535);
  }
}
/*
 * FUNCTION: syncAntennaNames
 * ---------------------------
 * Writes all antenna names from myConfig to the Nextion
 * label components t3-t6. Called on boot and after rename.
 */
/*
 * FUNCTION: syncAntennaNames
 * ---------------------------
 * Writes antenna names to Nextion label components t3-t10.
 * Labels beyond stored RELAY_COUNT get a generic "ANT n" name.
 * Called on boot and after any rename.
 */
void syncAntennaNames() {
  int rows = getRowCount();
  for (int i = 0; i < rows; i++) {
    String name = (i < RELAY_COUNT) ? String(myConfig.names[i]) : "ANT " + String(i + 1);
    myNex.writeStr("t" + String(i + 3) + ".txt", name);
  }
}


/*
 * FUNCTION: updateAntennaStatus
 * ------------------------------
 * Updates the tState text component on the Nextion to reflect
 * whether an antenna is currently active or all are grounded.
 * Also updates the LED matrix.
 */
void updateAntennaStatus() {
  bool anyActive   = false;
  int  activeIndex = -1;
  for (int i = 0; i < RELAY_COUNT; i++) {
    if (digitalRead(relayPins[i]) == HIGH) { anyActive = true; activeIndex = i; break; }
  }
  if (anyActive) {
    myNex.writeStr("tState.txt", "ANT Active");
    myNex.writeStr("tState.pco", "63488");   // Red text for active state
    updateMatrix(activeIndex + 1);
  } else {
    myNex.writeStr("tState.txt", "ANT Grounded");
    myNex.writeStr("tState.pco", "1024");    // Green text for grounded state
    updateMatrix(0);
    displayGroundSymbol();
  }
}

/*
 * FUNCTION: controlRelay
 * ----------------------
 * Activates or deactivates a relay by 1-based antenna index.
 * When state == HIGH, grounds all other relays first (single
 * antenna rule). Updates portA.rxAntenna to track selection.
 *
 * Note: Only Input 1 (portA) drives physical relays on the
 * original 4-port shield. Port B relay support requires the
 * KK1L expansion board.
 */
void controlRelay(int targetIndex, bool state) {
  int idx = targetIndex - 1;
  if (idx < 0 || idx >= RELAY_COUNT) return;

  if (state == HIGH) {
    currentActiveRelay    = targetIndex;
    portA.rxAntenna       = targetIndex;   // track Input 1 selection
    for (int i = 0; i < RELAY_COUNT; i++) {
      digitalWrite(relayPins[i], (i == idx) ? HIGH : LOW);
    }
  } else {
    digitalWrite(relayPins[idx], LOW);
    if (currentActiveRelay == targetIndex) {
      currentActiveRelay = 0;
      portA.rxAntenna    = 0;
    }
  }

  syncButtonStates();
  syncAntennaNames();
  updateAntennaStatus();
  updateNextionBandDisplay();
}
/*
 * FUNCTION: updateNextionBandDisplay
 * ------------------------------------
 * Pushes the current band for each input port and the SO2R
 * interlock status to the Nextion HMI components:
 *   tBandA — band name for Input 1 / Slice A
 *   tBandB — band name for Input 2 / Slice B
 *   tSO2R  — "INHIBIT" (orange) if either port is inhibited,
 *             "SO2R OK" (green) otherwise
 *
 * Called after any relay change, band update, or interlock
 * evaluation.
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


// ============================================================
//  SO2R INTERLOCK
// ============================================================

/*
 * FUNCTION: evaluateInterlock
 * ----------------------------
 * Evaluates the SO2R interlock conditions and sets the
 * inhibited flag and reason on portA/portB as appropriate.
 *
 * Interlock fires when BOTH ports are simultaneously
 * transmitting AND one of the following conflicts exists:
 *   1. Same antenna — portB is inhibited
 *   2. Same band    — portB is inhibited
 *
 * If only one port is transmitting, no interlock is applied.
 * Call updateNextionBandDisplay() after this function to
 * push the updated status to the Nextion.
 *
 * Future: PA protection sequencer will also subscribe to
 * the inhibited state. TX slice tracking from SmartSDR
 * (via Node-RED) will feed portA.transmitting / portB.transmitting.
 */
void evaluateInterlock() {
  // Clear any previous inhibit state
  portA.inhibited = false;
  portB.inhibited = false;
  portA.inhibitReason[0] = '\0';
  portB.inhibitReason[0] = '\0';

  // Only evaluate when both ports are actively transmitting
  if (!portA.transmitting || !portB.transmitting) return;

  // Conflict 1: Both ports using the same physical antenna
  if (portA.txAntenna != 0 && portA.txAntenna == portB.txAntenna) {
    portB.inhibited = true;
    snprintf(portB.inhibitReason, sizeof(portB.inhibitReason),
      "same antenna as Radio A (ant %d)", portB.txAntenna);
    Serial.println(F("[INTERLOCK] Same antenna — Radio B inhibited"));
    return;
  }

  // Conflict 2: Both ports on the same band (interference risk)
  if (portA.band != 0 && portA.band == portB.band) {
    portB.inhibited = true;
    snprintf(portB.inhibitReason, sizeof(portB.inhibitReason),
      "same band as Radio A (%s)", bandName(portB.band));
    Serial.println(F("[INTERLOCK] Same band — Radio B inhibited"));
  }
}


// ============================================================
//  TCP PROTOCOL
// ============================================================

/*
 * FUNCTION: sendTcpResponse
 * --------------------------
 * Sends a formatted response line to the connected TCP client.
 * Format: R[seq]|[code hex]|[body]
 * Code 0x00 = OK, 0x404 = not found, 0x500 = error.
 */
void sendTcpResponse(int seq, int code, const char* body) {
  if (!tcpClient || !tcpClient.connected()) return;
  char line[256];
  snprintf(line, sizeof(line), "R%d|%02X|%s", seq, code, body);
  tcpClient.println(line);
}

/*
 * FUNCTION: pushPortStatus
 * -------------------------
 * Sends an unsolicited status event for a port to the TCP
 * client. Format: S0|port id=... auto=... band=... ...
 * Called after any state change to keep the client in sync.
 */
void pushPortStatus(PortState* p) {
  if (!tcpClient || !tcpClient.connected()) return;
  char line[192];
  snprintf(line, sizeof(line),
    "S0|port id=%d auto=%d band=%d rxant=%d txant=%d tx=%d inhibited=%d reason=%s",
    p->portId, p->autoMode ? 1 : 0, p->band,
    p->rxAntenna, p->txAntenna,
    p->transmitting ? 1 : 0,
    p->inhibited    ? 1 : 0,
    p->inhibitReason
  );
  tcpClient.println(line);
}

/*
 * FUNCTION: portById
 * -------------------
 * Returns a pointer to portA or portB by 1-based ID.
 * Returns nullptr for any other ID.
 */
PortState* portById(int id) {
  if (id == 1) return &portA;
  if (id == 2) return &portB;
  return nullptr;
}

/*
 * FUNCTION: parseParam
 * ---------------------
 * Simple key=value parser for TCP command parameter strings.
 * Searches for "key=" within str and returns the integer
 * value that follows. Returns -1 if the key is not found.
 * Example: parseParam("rxant=3 txant=3", "rxant") -> 3
 */
int parseParam(const char* str, const char* key) {
  char* found = strstr(str, key);
  if (!found) return -1;
  found += strlen(key);
  if (*found == '=') found++;
  return atoi(found);
}

/*
 * FUNCTION: processTcpCommand
 * ----------------------------
 * Parses and executes a single TCP command line.
 * Lines must be in the format: C[seq]|<command> [params]
 *
 * Supported commands:
 *   ping                          — keepalive check
 *   antenna list                  — list all configured antennas
 *   band list                     — list all defined bands with freq ranges
 *   port get <id>                 — get full state of a port
 *   port set <id> [params]        — set port parameters, drive relay if rxant given
 *   sub port all                  — subscribe to port status events
 *   interlock set [params]        — update TX state and trigger interlock evaluation
 */
void processTcpCommand(const char* line) {
  if (line[0] != 'C') return;

  // Parse sequence number from C[seq]|
  int seq = 0;
  int i   = 1;
  while (line[i] && line[i] != '|') { seq = seq * 10 + (line[i] - '0'); i++; }
  if (line[i] != '|') return;
  const char* cmd = &line[i + 1];

  Serial.print(F("[TCP] seq=")); Serial.print(seq);
  Serial.print(F(" cmd="));     Serial.println(cmd);

  // ── ping ──────────────────────────────────────────────────
  if (strncmp(cmd, "ping", 4) == 0) {
    sendTcpResponse(seq, 0, "");

  // ── antenna list ──────────────────────────────────────────
  } else if (strncmp(cmd, "antenna list", 12) == 0) {
    for (int a = 0; a < RELAY_COUNT; a++) {
      char body[96];
      snprintf(body, sizeof(body),
        "id=%d name=%s txBandMask=FFFF rxBandMask=FFFF rxOnly=0", a + 1, myConfig.names[a]);
      sendTcpResponse(seq, 0, body);
    }
    sendTcpResponse(seq, 0, "");

  // ── band list ─────────────────────────────────────────────
  } else if (strncmp(cmd, "band list", 9) == 0) {
    for (int b = 0; b < BAND_COUNT; b++) {
      char body[64];
      snprintf(body, sizeof(body), "id=%d name=%s freqStart=%.3f freqStop=%.3f",
        BANDS[b].id, BANDS[b].name, BANDS[b].freqStartMhz, BANDS[b].freqStopMhz);
      sendTcpResponse(seq, 0, body);
    }
    sendTcpResponse(seq, 0, "");

  // ── port get ──────────────────────────────────────────────
  } else if (strncmp(cmd, "port get ", 9) == 0) {
    int portId   = atoi(&cmd[9]);
    PortState* p = portById(portId);
    if (!p) { sendTcpResponse(seq, 0x404, "invalid port"); return; }
    char body[128];
    snprintf(body, sizeof(body),
      "id=%d auto=%d band=%d rxant=%d txant=%d tx=%d inhibited=%d reason=%s",
      p->portId, p->autoMode ? 1 : 0, p->band, p->rxAntenna, p->txAntenna,
      p->transmitting ? 1 : 0, p->inhibited ? 1 : 0, p->inhibitReason);
    sendTcpResponse(seq, 0, body);

  // ── port set ──────────────────────────────────────────────
  // Format: "port set <portId> rxant=<n> txant=<n> auto=<0|1> band=<n>"
  // Note: In the current v1.5 hardware (original 4-port shield), only
  // port A (portId=1) drives the relay bank. Port B relay support
  // requires the KK1L 2x6 expansion board (see roadmap).
  } else if (strncmp(cmd, "port set ", 9) == 0) {
    const char* params  = &cmd[9];
    int portId   = atoi(params);
    int rxAnt    = parseParam(params, "rxant");
    int txAnt    = parseParam(params, "txant");
    int autoMode = parseParam(params, "auto");
    int band     = parseParam(params, "band");

    PortState* p = portById(portId);
    if (!p) { sendTcpResponse(seq, 0x404, "invalid port"); return; }

    if (autoMode >= 0) p->autoMode = (autoMode == 1);
    if (band > 0)      p->band = band;

    if (rxAnt > 0 && rxAnt <= RELAY_COUNT) {
      p->rxAntenna = rxAnt;
      if (portId == 1) {
        // Only port A drives relays on the original shield
        controlRelay(rxAnt, HIGH);
        Serial.print(F("[RELAY] Port A -> Antenna ")); Serial.println(rxAnt);
      }
      // TODO: Port B relay bank when KK1L expansion board fitted
    }
    if (txAnt > 0 && txAnt <= RELAY_COUNT) p->txAntenna = txAnt;

    evaluateInterlock();
    sendTcpResponse(seq, 0, "ok");
    pushPortStatus(p);
    updateNextionBandDisplay();

  // ── sub port all ──────────────────────────────────────────
  } else if (strncmp(cmd, "sub port all", 12) == 0) {
    sendTcpResponse(seq, 0, "subscribed");

  // ── interlock set ─────────────────────────────────────────
  // Updates TX state and optionally band for each port.
  // Triggers interlock evaluation and pushes updated status.
  // Format: "interlock set radioA=<0|1> radioB=<0|1> band=<n>"
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

  // ── unknown command ───────────────────────────────────────
  } else {
    sendTcpResponse(seq, 0x500, "unknown command");
  }
}

/*
 * FUNCTION: handleTCP
 * --------------------
 * Called from loop(). Accepts new TCP client connections on
 * port 9008 and reads incoming lines. Only one client is
 * supported at a time — a new connection replaces the old one.
 *
 * On new connection, sends the device greeting:
 *   S0|device name=ShackSwitch ready=1
 */
void handleTCP() {
  // Accept a new client if none is connected
  if (!tcpClient || !tcpClient.connected()) {
    tcpClient = tcpServer.available();
    if (tcpClient) {
      Serial.println(F("[TCP] Client connected"));
      tcpLinePos = 0;
      tcpClient.println(F("S0|device name=ShackSwitch ready=1"));
    }
    return;
  }

  // Read available bytes into line buffer
  while (tcpClient.available()) {
    char c = tcpClient.read();
    if (c == '\n') {
      // Strip trailing \r if present
      tcpLineBuffer[tcpLinePos] = '\0';
      if (tcpLinePos > 0 && tcpLineBuffer[tcpLinePos - 1] == '\r')
        tcpLineBuffer[--tcpLinePos] = '\0';
      if (tcpLinePos > 0) processTcpCommand(tcpLineBuffer);
      tcpLinePos = 0;
    } else if (tcpLinePos < (int)sizeof(tcpLineBuffer) - 1) {
      tcpLineBuffer[tcpLinePos++] = c;
    }
  }
}


// ============================================================
//  UDP DISCOVERY BEACON
// ============================================================

/*
 * FUNCTION: handleDiscoveryBeacon
 * --------------------------------
 * Broadcasts a UDP discovery packet every BEACON_INTERVAL ms
 * to the local subnet broadcast address on port 9008.
 *
 * Payload format:
 *   SS name=ShackSwitch serial=SS-001 version=1.5.0
 *      ip=[IP] port=9008 ant=[count] radio=2
 *
 * This allows AetherSDR and other network clients to
 * auto-discover the ShackSwitch without manual IP entry.
 */
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


// ============================================================
//  WIFI & CONFIG
// ============================================================

/*
 * FUNCTION: loadConfig
 * ---------------------
 * Reads RelayConfig from EEPROM. If the magic value does not
 * match CONFIG_VERSION (indicating a blank or corrupt EEPROM),
 * writes factory defaults and saves them.
 */
void loadConfig() {
  EEPROM.get(0, myConfig);
 
  if (myConfig.configMagic != CONFIG_VERSION) {
    Serial.println(F("EEPROM empty or version mismatch — loading factory defaults"));
    for (int i = 0; i < 8; i++) {    // hard code set to 8 not relay_count
      String dName = "Relay " + String(i + 1);
      dName.toCharArray(myConfig.names[i], 26);
    }
    strncpy(myConfig.wifiSSID, "tinkerbell", 33);
    strncpy(myConfig.wifiPass, "disneybell", 64);
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
  }
}

/*
 * FUNCTION: connectToWiFi
 * ------------------------
 * Attempts to connect to the configured WiFi network with a
 * 15-second timeout. Connection status is printed to Serial.
 */
void connectToWiFi() {
  WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
  Serial.print(F("Connecting to: ")); Serial.println(myConfig.wifiSSID);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    delay(500);
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println(F("\nWiFi connection timed out."));
  }
}

/*
 * FUNCTION: syncTime
 * -------------------
 * Fetches current time via NTP and sets the onboard RTC.
 * Called once on boot after WiFi connects, and again at
 * midnight to keep the clock accurate.
 */
void syncTime() {
  timeClient.begin();
  if (timeClient.update()) {
    RTCTime currentTime(timeClient.getEpochTime());
    RTC.setTime(currentTime);
    Serial.println(F("NTP sync complete."));
  }
}

/*
 * FUNCTION: updateStationMonitor
 * --------------------------------
 * Updates the WiFi RSSI and signal quality bar on the Nextion
 * monitor page. Called periodically and when entering the
 * monitor page.
 */
void updateStationMonitor() {
  if (WiFi.status() == WL_CONNECTED) {
    currentRSSI = WiFi.RSSI();
    int signalQuality = map(currentRSSI, -100, -40, 0, 100);
    signalQuality = constrain(signalQuality, 0, 100);
    myNex.writeStr("tRSSI.txt", String(currentRSSI) + " dBm");
    myNex.writeNum("nSignal.val", signalQuality);
    myNex.writeStr("b0.txt", WiFi.localIP().toString());
    if      (currentRSSI > -60) myNex.writeNum("nSignal.pco", NEXTION_GREEN);
    else if (currentRSSI > -80) myNex.writeNum("nSignal.pco", NEXTION_RED);
  }
}


// ============================================================
//  WEB PAGES
// ============================================================

/*
 * FUNCTION: showMainPage
 * -----------------------
 * Serves the main antenna switcher web page. Each antenna port
 * is displayed as a card with a toggle button. The page
 * auto-polls /status every 5 seconds via JavaScript to reflect
 * relay state changes made from the Nextion touchscreen.
 * Also displays current band and SO2R status.
 */
void showMainPage(WiFiClient& c) {
  // Determine row count from stored port mode
  int rowCount = 4;
  if (myConfig.portMode == 1) rowCount = 6;
  if (myConfig.portMode == 2) rowCount = 8;

  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n");
  c.println("<!DOCTYPE HTML><html>");
  c.println("<head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  c.println("<style>");
  c.println("*{box-sizing:border-box;margin:0;padding:0;}");
  c.println("body{font-family:Arial;background:#1a1a1a;color:white;padding:10px;}");
  c.println("h1{text-align:center;font-size:18px;margin-bottom:10px;color:#aaa;}");
  c.println(".layout{display:flex;gap:10px;max-width:900px;margin:0 auto;}");
  c.println(".matrix{flex:1;}");
  c.println(".row{display:flex;align-items:center;gap:6px;margin-bottom:6px;}");
  c.println(".antname{width:120px;flex-shrink:0;background:#333;border-radius:6px;padding:8px 10px;font-size:13px;font-weight:bold;text-align:center;overflow:hidden;white-space:nowrap;text-overflow:ellipsis;}");
  c.println(".sel{width:50px;padding:8px 0;border:none;border-radius:6px;font-size:12px;font-weight:bold;color:white;cursor:pointer;text-decoration:none;display:block;text-align:center;}");
  c.println(".sel-a{background:#555;} .sel-b{background:#555;}");
  c.println(".sel-a.active{background:#28a745;} .sel-b.active{background:#ff9800;}");
  c.println(".status{width:180px;flex-shrink:0;background:#1a2a3a;border-radius:10px;padding:12px;font-size:13px;line-height:2;}");
  c.println(".status h2{font-size:14px;color:#aaa;margin-bottom:8px;text-align:center;}");
  c.println(".so2rok{color:#28a745;font-weight:bold;} .so2rwarn{color:#ff9800;font-weight:bold;}");
  c.println(".lbl{color:#888;font-size:11px;}");
  c.println(".mode{text-align:center;font-size:10px;color:#555;margin-top:8px;}");
  c.println("a.settings{display:block;text-align:center;color:#555;margin-top:10px;font-size:12px;text-decoration:none;}");
  c.println("</style></head><body>");

  // Mode label in title for debug clarity
  String modeLabel = "4-port";
  if (myConfig.portMode == 1) modeLabel = "2x6";
  if (myConfig.portMode == 2) modeLabel = "2x8";
  c.println("<h1>G0JKN ShackSwitch v1.5 <span style='color:#555;font-size:13px;'>[" + modeLabel + "]</span></h1>");

  c.println("<div class='layout'>");

  // ── Antenna matrix ─────────────────────────────────────
  c.println("<div class='matrix'>");
  for (int i = 0; i < rowCount; i++) {
    bool aActive = (portA.rxAntenna == i + 1);
    bool bActive = (portB.rxAntenna == i + 1);
    String aClass = aActive ? " active" : "";
    String bClass = bActive ? " active" : "";

    // Antenna name — use stored name if within RELAY_COUNT, else generic label
    String antLabel = (i < RELAY_COUNT) ? String(myConfig.names[i]) : "ANT " + String(i + 1);

    c.print("<div class='row'>");
    c.print("<a href='/a/" + String(i+1) + "/sel' class='sel sel-a" + aClass + "' id='bA" + String(i+1) + "'>1</a>");
    c.print("<div class='antname'>" + antLabel + "</div>");
    c.print("<a href='/b/" + String(i+1) + "/sel' class='sel sel-b" + bClass + "' id='bB" + String(i+1) + "'>2</a>");
    c.println("</div>");
  }
  c.println("</div>"); // end matrix

  // ── Status panel ───────────────────────────────────────
  bool inhibit = portA.inhibited || portB.inhibited;
  c.println("<div class='status'>");
  c.println("<h2>Status</h2>");
  c.println("<div class='lbl'>Input 1</div>");
  c.println("<div id='bandA'>" + String(bandName(portA.band)) + "</div>");
  c.println("<div class='lbl'>Input 2</div>");
  c.println("<div id='bandB'>" + String(bandName(portB.band)) + "</div>");
  c.println("<div class='lbl'>SO2R</div>");
  c.println("<div id='so2r' class='" + String(inhibit ? "so2rwarn" : "so2rok") + "'>");
  c.println(inhibit ? "&#9888; INHIBIT" : "OK");
  c.println("</div>");
  c.println("<div class='lbl' style='margin-top:8px;'>IP</div>");
  c.println("<div style='font-size:11px;'>" + WiFi.localIP().toString() + "</div>");
  c.println("<div class='mode'>mode: " + modeLabel + "</div>");
  c.println("</div>"); // end status

  c.println("</div>"); // end layout
  c.println("<a href='/settings' class='settings'>[ SETTINGS ]</a>");

  // ── JavaScript — poll /status every 5 seconds ──────────
  c.println("<script>");
  c.println("const rows=" + String(rowCount) + ";");
  c.println("function updateStatus(){");
  c.println(" fetch('/status').then(r=>r.json()).then(d=>{");
  c.println("  for(let i=1;i<=rows;i++){");
  c.println("   const bA=document.getElementById('bA'+i);");
  c.println("   const bB=document.getElementById('bB'+i);");
  c.println("   if(bA)bA.className='sel sel-a'+(d['a'+i]===1?' active':'');");
  c.println("   if(bB)bB.className='sel sel-b'+(d['b'+i]===1?' active':'');");
  c.println("  }");
  c.println("  document.getElementById('bandA').textContent=d.bandA||'---';");
  c.println("  document.getElementById('bandB').textContent=d.bandB||'---';");
  c.println("  const inh=d.so2r===1;");
  c.println("  const s=document.getElementById('so2r');");
  c.println("  s.textContent=inh?'\\u26a0 INHIBIT':'OK';");
  c.println("  s.className=inh?'so2rwarn':'so2rok';");
  c.println(" }).catch(()=>{});");
  c.println("}");
  c.println("setInterval(updateStatus,5000);");
  c.println("</script>");
  c.println("</body></html>");
}
void showSettingsPage(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n");
  c.println("<!DOCTYPE HTML><html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head>");
  c.println("<body style='font-family:Arial;padding:20px;background:#1a1a1a;color:white;'>");
  c.println("<h2>ShackSwitch Settings</h2>");

  // ── Debug — port mode selector ──────────────────────────
  c.println("<div style='background:#2a1a1a;border:1px solid #ff9800;border-radius:8px;padding:15px;margin-bottom:20px;'>");
  c.println("<strong style='color:#ff9800;'>&#9888; Debug — Port Mode</strong>");
  c.println("<form action='/setmode' style='margin-top:10px;'>");
  c.println("<label style='margin-right:20px;'><input type='radio' name='mode' value='0'" + String(myConfig.portMode == 0 ? " checked" : "") + "> 4-port (original)</label>");
  c.println("<label style='margin-right:20px;'><input type='radio' name='mode' value='1'" + String(myConfig.portMode == 1 ? " checked" : "") + "> 2x6 matrix</label>");
  c.println("<label style='margin-right:20px;'><input type='radio' name='mode' value='2'" + String(myConfig.portMode == 2 ? " checked" : "") + "> 2x8 matrix</label>");
  c.println("<br><br><button type='submit' style='background:#ff9800;color:white;border:none;padding:8px 20px;border-radius:6px;cursor:pointer;'>Apply Mode</button>");
  c.println("</form></div>");

  // ── Antenna names ────────────────────────────────────────
  c.println("<h3>Antenna Names</h3>");
  for (int i = 0; i < RELAY_COUNT; i++) {
    c.print("<div style='margin-bottom:15px;'><strong>ANT " + String(i+1) + ": " + String(myConfig.names[i]) + "</strong>");
    c.print("<form action='/rename'>");
    c.print("<input type='hidden' name='id' value='" + String(i+1) + "'>");
    c.print("<input type='text' name='name' maxlength='25' style='margin:0 8px;padding:4px;'>");
    c.print("<button type='submit'>Save</button></form></div>");
  }
  c.println("<br><a href='/' style='color:#666;'>Back</a></body></html>");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }
  Serial.println(F("G0JKN ShackSwitch v1.5 starting..."));

  matrix.begin();
  myNex.begin(9600);
  delay(1500);             // Give Nextion time to finish booting
  loadConfig();
  RTC.begin();

  // Ground all relays on boot
  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  connectToWiFi();

  // Start all servers and UDP listener
  server.begin();
  tcpServer.begin();
  discoveryUDP.begin(DISCOVERY_PORT);

  syncTime();

  // Initialise Nextion display state — page switch LAST
  // after everything else is ready
  // Initialise Nextion display state — page switch LAST
  // Extra delay for 7" Nextion boot time
 
  delay(3000);

  switch (myConfig.portMode) {
    case 1:
      Serial.println(F("[SETUP] Sending page 1"));
      myNex.writeStr("page 1");
      break;
    case 2:
      Serial.println(F("[SETUP] Sending page 2"));
      myNex.writeStr("page 2");
      break;
    default:
      Serial.println(F("[SETUP] Sending page 0"));
      myNex.writeStr("page 0");
      break;
  }
  delay(200);
  syncButtonStates();
  syncAntennaNames();
  myNex.writeStr("t1.txt", WiFi.localIP().toString());
  updateNextionBandDisplay();

  Serial.print(F("Web server: port 80  TCP: port 9008  IP: "));
  Serial.println(WiFi.localIP());
  Serial.println(F("[SETUP] Complete"));
}
// ============================================================
//  LOOP
// ============================================================
void loop() {

  // ── Nextion event handling ─────────────────────────────────
  // Processes touch events and trigger callbacks from the
  // Nextion display. Trigger functions are in triggers.ino.
  myNex.NextionListen();

  // ── TCP control protocol ───────────────────────────────────
  handleTCP();

  // ── UDP discovery beacon ───────────────────────────────────
  handleDiscoveryBeacon();

  // ── TCP keepalive ──────────────────────────────────────────
  // Sends S0|ping every 30 seconds to keep the TCP connection
  // alive and allow the client to detect disconnection.
  if (tcpClient && tcpClient.connected()) {
    if (millis() - lastKeepalive > KEEPALIVE_INTERVAL) {
      lastKeepalive = millis();
      tcpClient.println(F("S0|ping"));
    }
  }

  // ── RSSI & monitor page update ─────────────────────────────
  if (millis() - lastRSSIUpdate > 10000) {
    if (onMonitorPage || WiFi.status() == WL_CONNECTED) updateStationMonitor();
    lastRSSIUpdate = millis();
  }

  // ── Factory reset safety timeout ───────────────────────────
  // If the first reset tap is not confirmed within 5 seconds,
  // cancel the reset and clear the warning on the Nextion.
  if (resetConfirmed && (millis() - resetTimer > 5000)) {
    resetConfirmed = false;
    myNex.writeStr("tStatus.txt", "Reset Canceled");
  }

  // ── Background WiFi reconnect ──────────────────────────────
  // Attempts reconnection every 30 seconds if WiFi is lost.
  if (WiFi.status() != WL_CONNECTED && !onConfigPage) {
    if (millis() - lastWiFiCheck > 30000) {
      WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
      lastWiFiCheck = millis();
    }
    myNex.writeStr("t1.txt", "N O T   C O N N E C T E D");
  }

  // ── RTC clock display ──────────────────────────────────────
  // Updates the tClock component on the Nextion every 25s.
  // Re-syncs NTP at midnight.
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

  // ── Web server ─────────────────────────────────────────────
  // Handles one HTTP request per loop iteration. Reads the
  // first line of the request (GET /path HTTP/1.1) and routes
  // to the appropriate handler. All responses use raw
  // WiFiClient.println() — no web framework is used.
  WiFiClient webClient = server.available();
  if (webClient) {
    String request = webClient.readStringUntil('\r');
    webClient.flush();

    // ── GET /status ─────────────────────────────────────────
    // Returns relay states, current band for each input, and
    // SO2R interlock status as JSON.
    if (request.indexOf("GET /status") != -1) {
      String json = "{";
      // Relay states r1-r4 (physical relay pins)
      for (int i = 0; i < RELAY_COUNT; i++) {
        json += "\"r" + String(i+1) + "\":" + String(digitalRead(relayPins[i]));
        json += ",";
      }
      // Input 1 antenna selection (1-based, 0=none)
      for (int i = 1; i <= RELAY_COUNT; i++) {
        json += "\"a" + String(i) + "\":" + String(portA.rxAntenna == i ? 1 : 0);
        json += ",";
      }
      // Input 2 antenna selection
      for (int i = 1; i <= RELAY_COUNT; i++) {
        json += "\"b" + String(i) + "\":" + String(portB.rxAntenna == i ? 1 : 0);
        if (i < RELAY_COUNT) json += ",";
      }
      json += ",\"bandA\":\"" + String(bandName(portA.band)) + "\"";
      json += ",\"bandB\":\"" + String(bandName(portB.band)) + "\"";
      json += ",\"so2r\":"   + String((portA.inhibited || portB.inhibited) ? "1" : "0");
      json += "}";
      webClient.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + json);
    }

else if (request.indexOf("GET /setmode") != -1) {
  int modeIdx = request.indexOf("mode=") + 5;
  int mode    = request.substring(modeIdx, modeIdx + 1).toInt();
  
  Serial.print(F("[SETMODE] Received mode="));
  Serial.println(mode);
  
  if (mode >= 0 && mode <= 2) {
    myConfig.portMode    = mode;
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
    
    // Verify EEPROM write by reading back
    RelayConfig verify;
    EEPROM.get(0, verify);
   

    switch (mode) {
      case 0:
        Serial.println(F("[SETMODE] Switching to page 0 (4-port)"));
        myNex.writeStr("page 0");
        break;
      case 1:
        Serial.println(F("[SETMODE] Switching to page 1 (2x6)"));
        myNex.writeStr("page 1");
        break;
      case 2:
        Serial.println(F("[SETMODE] Switching to page 2 (2x8)"));
        myNex.writeStr("page 2");
        break;
    }
    syncButtonStates();
    syncAntennaNames();
    updateNextionBandDisplay();
  } else {
    Serial.print(F("[SETMODE] Invalid mode rejected: "));
    Serial.println(mode);
  }
  webClient.println("HTTP/1.1 303 See Other\r\nLocation: /settings\r\n\r\n");
}


else if (request.indexOf("GET /a/") != -1 && request.indexOf("/sel") != -1) {
      // Parse antenna number from /a/[n]/sel
      int aIdx = request.indexOf("GET /a/") + 7;
      int ant  = request.substring(aIdx, aIdx + 1).toInt();

      if (ant >= 1 && ant <= RELAY_COUNT) {
        // Toggle — if already selected, deselect
        if (portA.rxAntenna == ant) {
          portA.rxAntenna = 0;
          controlRelay(ant, LOW);
        } else {
          portA.rxAntenna = ant;
          controlRelay(ant, HIGH);
        }
        evaluateInterlock();
        updateNextionBandDisplay();
        Serial.print(F("[WEB] Input 1 -> ANT ")); Serial.println(ant);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
    }

else if (request.indexOf("GET /b/") != -1 && request.indexOf("/sel") != -1) {
      // Parse antenna number from /b/[n]/sel
      int bIdx = request.indexOf("GET /b/") + 7;
      int ant  = request.substring(bIdx, bIdx + 1).toInt();

      if (ant >= 1 && ant <= RELAY_COUNT) {
        // Toggle — if already selected, deselect
        if (portB.rxAntenna == ant) {
          portB.rxAntenna = 0;
          // Only ground relay if Input 1 isn't also using it
          if (portA.rxAntenna != ant) controlRelay(ant, LOW);
        } else {
          // Check interlock — can't use same antenna as Input 1
          if (portA.rxAntenna == ant) {
            // Blocked — redirect back without change
            webClient.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
            return; // Note: won't work inside if/else chain — see note below
          }
          portB.rxAntenna = ant;
          // TODO: Port B relay bank requires KK1L board
          // For now just tracks selection without driving relay
        }
        evaluateInterlock();
        updateNextionBandDisplay();
        Serial.print(F("[WEB] Input 2 -> ANT ")); Serial.println(ant);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
    }

    // ── GET /settings ────────────────────────────────────────
    else if (request.indexOf("GET /settings") != -1) {
      showSettingsPage(webClient);
    }

    // ── GET /setband ─────────────────────────────────────────
    // Sets the current band for Input 1 or Input 2 by name.
    // Intended to be called by Node-RED when SmartSDR reports
    // a band change on a TX slice.
    //
    // Parameters:
    //   input=1|2   — which input port to update
    //   band=40m    — band name string (must match BANDS[] exactly)
    //
    // Example: GET /setband?input=1&band=40m
    // Response: {"input":1,"band":"40m","bandId":4,"so2r":false}
    else if (request.indexOf("GET /setband") != -1) {
      int    input   = 0;
      String bandStr = "";

      // Parse ?input= from URL
      int inputIdx = request.indexOf("input=");
      if (inputIdx != -1) {
        input = request.substring(inputIdx + 6, inputIdx + 7).toInt();
      }

      // Parse &band= from URL
      int bandIdx = request.indexOf("band=");
      if (bandIdx != -1) {
        int bandEnd = request.indexOf(' ', bandIdx);
        bandStr = request.substring(bandIdx + 5, bandEnd);
      }

      // Reverse lookup: band name string -> band ID integer
      int bandId = 0;
      for (int i = 0; i < BAND_COUNT; i++) {
        if (bandStr == BANDS[i].name) { bandId = BANDS[i].id; break; }
      }

      if (input == 0 || bandStr == "" || bandId == 0) {
        webClient.println("HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"error\":\"missing or unrecognised params\"}");
      } else {
        if (input == 1) portA.band = bandId;
        else if (input == 2) portB.band = bandId;

        evaluateInterlock();
        updateNextionBandDisplay();

        String response = "{\"input\":"  + String(input)  +
                          ",\"band\":\"" + bandStr         + "\"" +
                          ",\"bandId\":" + String(bandId)  +
                          ",\"so2r\":"   + String((portA.inhibited || portB.inhibited) ? "true" : "false") + "}";
        webClient.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + response);
      }
    }

    // ── GET /rename ──────────────────────────────────────────
    // Renames an antenna port. Persists to EEPROM and updates
    // the Nextion label immediately.
    // Parameters: id=1-4, name=[text] (URL encoded, + for space)
    else if (request.indexOf("GET /rename") != -1) {
      int    idPos  = request.indexOf("id=") + 3;
      int    rId    = request.substring(idPos, idPos + 1).toInt();
      int    namePos = request.indexOf("name=") + 5;
      int    endPos  = request.indexOf(" ", namePos);
      String nN      = request.substring(namePos, endPos);
      nN.replace("+",   " ");
      nN.replace("%20", " ");

      if (rId >= 1 && rId <= RELAY_COUNT) {
        nN.toCharArray(myConfig.names[rId - 1], 26);
        myConfig.configMagic = CONFIG_VERSION;
        EEPROM.put(0, myConfig);

        // Update Nextion label (t3=ANT1, t4=ANT2, t5=ANT3, t6=ANT4)
        myNex.writeStr("t" + String(rId + 2) + ".txt", nN);

        Serial.print(F("RENAME id="));   Serial.println(rId);
        Serial.print(F("RENAME name=")); Serial.println(nN);
        Serial.print(F("RENAME Nextion target=t")); Serial.println(rId + 2);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /settings\r\n\r\n");
    }

    // ── GET /[n]/on and /[n]/off ─────────────────────────────
    // Activates or grounds a specific relay by number.
    // Redirects back to / after actioning so the browser
    // returns to the main page.
    else if (request.indexOf("/on") != -1 || request.indexOf("/off") != -1) {
      for (int i = 1; i <= RELAY_COUNT; i++) {
        if (request.indexOf("/" + String(i) + "/on")  != -1) controlRelay(i, HIGH);
        if (request.indexOf("/" + String(i) + "/off") != -1) controlRelay(i, LOW);
      }
      webClient.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
    }

    // ── Default — serve main page ────────────────────────────
    else {
      showMainPage(webClient);
    }

    webClient.stop();
  }
}
