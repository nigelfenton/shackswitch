/* 
 * VERSION 1.3 - G0JKN Antenna Switcher 
 * Changes from 1.2:
 * - Removed blocking while(!Serial) wait on boot
 * - Added splash screen and step-by-step boot status on Nextion t1.txt
 * - Improved WiFi reconnect with live countdown and state flag
 */

#include "WiFiS3.h"
#include "EasyNextionLibrary.h"
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
#include "RTC.h"
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

struct RelayConfig { 
  char names[4][26]; 
  char wifiSSID[33];      
  char wifiPass[64];
  uint32_t configMagic;   
};

RelayConfig myConfig;
const uint32_t CONFIG_VERSION = 0xDEADBEEE;

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

const int relayPins[] = {2, 3, 4, 5};
const uint32_t NEXTION_GREEN = 1024;
const uint32_t NEXTION_RED = 63488;

WiFiServer server(80);

unsigned long lastRSSIUpdate = 0;
int currentRSSI = 0;
bool onMonitorPage = false;

// Logic Timers & States
unsigned long lastClockUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long resetTimer = 0;
bool resetConfirmed = false;
bool onConfigPage = false;
int currentActiveRelay = 0;
bool wifiWasConnected = false;  // Tracks WiFi state for reconnect display


// --- CORE FUNCTIONS ---

void updateStationMonitor() {
  if (WiFi.status() == WL_CONNECTED) {
    currentRSSI = WiFi.RSSI();
    
    // Convert dBm to a 0-100 percentage (roughly)
    int signalQuality = map(currentRSSI, -100, -40, 0, 100);
    signalQuality = constrain(signalQuality, 0, 100);

    // Update Nextion Page 3 labels
    myNex.writeStr("tRSSI.txt", String(currentRSSI) + " dBm");
    myNex.writeNum("nSignal.val", signalQuality);
    myNex.writeStr("b0.txt", WiFi.localIP().toString());

    if (currentRSSI > -60) myNex.writeNum("nSignal.pco", 1024);   // Green
    else if (currentRSSI > -80) myNex.writeNum("nSignal.pco", 63488); // Orange/Red
  }
}

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

/**
 * @brief Displays the ground schematic symbol on the LED Matrix.
 * Call this inside your 'else' block when 'anyActive' is false.
 */
void displayGroundSymbol() {
  matrix.loadFrame(ground_hex);
}

void loadConfig() {
  EEPROM.get(0, myConfig);
  if (myConfig.configMagic != CONFIG_VERSION) {
    Serial.println("EEPROM Empty. Loading Defaults...");
    for (int i = 0; i < 4; i++) {
      String dName = "Relay " + String(i + 1);
      dName.toCharArray(myConfig.names[i], 26);
    }
    strncpy(myConfig.wifiSSID, "tinkerbell", 33);
    strncpy(myConfig.wifiPass, "disneybell", 64);
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
  }
}

void controlRelay(int targetIndex, bool state) {
  int idx = targetIndex - 1;
  if (state == HIGH) {
    currentActiveRelay = targetIndex;
    for (int i = 0; i < 4; i++) {
      digitalWrite(relayPins[i], (i == idx) ? HIGH : LOW);
      myNex.writeStr("b" + String(i + 1) + ".bco=" + String((i == idx) ? NEXTION_GREEN : NEXTION_RED));
    }
  } else {
    digitalWrite(relayPins[idx], LOW);
    myNex.writeStr("b" + String(targetIndex) + ".bco=" + String(NEXTION_RED));
    if (currentActiveRelay == targetIndex) currentActiveRelay = 0;
  }
  updateAntennaStatus();
}

/* 
 * FUNCTION: updateAntennaStatus
 * ----------------------------
 * PURPOSE: Monitors 4 relay pins and updates the Nextion display and LED Matrix.
 * 
 * LOGIC FLOW:
 * 1. Scans relayPins array (Pins 2, 3, 4, 5).
 * 2. If a pin is HIGH, it maps the array index (0-3) to a human-readable 
 *    Antenna ID (1-4) using: (index + 1).
 * 3. Updates Nextion text (tState.txt) and color (tState.pco).
 *    - Active Color: 63488 (Red)
 *    - Grounded Color: 1024 (Green)
 * 4. Calls updateMatrix() with the ID (1-4) or 0 if none are active.
 * 
 * HARDWARE NOTE: Optimized for Arduino Uno R4.
 */
void updateAntennaStatus() {
  bool anyActive = false;
  int activeIndex = -1;

  for (int i = 0; i < 4; i++) {
    if (digitalRead(relayPins[i]) == HIGH) { 
      anyActive = true; 
      activeIndex = i;
      break; 
    }
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

void syncTime() {
  timeClient.begin();
  if(timeClient.update()) {
    RTCTime currentTime(timeClient.getEpochTime());
    RTC.setTime(currentTime);
    Serial.println("NTP Synced.");
  }
}

void connectToWiFi() {
  WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
  Serial.print("Connecting to: "); Serial.println(myConfig.wifiSSID);
  delay(5000);
  myNex.writeStr("t1.txt", WiFi.localIP().toString());
}


// --- WEB PAGES ---

void showMainPage(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n<!DOCTYPE HTML><html>");
  c.println("<head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:Arial;text-align:center;background:#1a1a1a;color:white;} .card{background:#333;margin:10px auto;padding:15px;width:90%;max-width:350px;border-radius:10px;} .btn{display:block;padding:15px;margin:10px 0;color:#fff;text-decoration:none;border-radius:8px;font-weight:bold;} .on{background:#28a745;} .off{background:#dc3545;}</style></head><body>");
  c.println("<h1>G0JKN 1.3 ANT SWITCH</h1>");
  for (int i = 0; i < 4; i++) {
    bool st = digitalRead(relayPins[i]);
    c.print("<div class='card'><strong>" + String(myConfig.names[i]) + "</strong>");
    if(st) c.print("<a href='/" + String(i+1) + "/off' class='btn on'>ACTIVE</a>");
    else c.print("<a href='/" + String(i+1) + "/on' class='btn off'>GROUNDED</a>");
    c.print("</div>");
  }
  c.println("<br><a href='/settings' style='color:#666;'>[ SETTINGS ]</a></body></html>");
}

void showSettingsPage(WiFiClient& c) {
  c.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n<!DOCTYPE HTML><html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head><body style='font-family:Arial;padding:20px;'>");
  c.println("<h2>Station Config</h2>");
  for (int i = 0; i < 4; i++) {
    c.print("<div style='margin-bottom:20px;'><strong>" + String(myConfig.names[i]) + "</strong>");
    c.print("<form action='/rename'><input type='hidden' name='id' value='"+String(i+1)+"'>");
    c.print("<input type='text' name='name' maxlength='25'><button type='submit'>Save</button></form></div>");
  }
  c.println("<a href='/'>Back</a></body></html>");
}


// --- SETUP & LOOP ---

void setup() {
  Serial.begin(115200);
  Serial.println("Debug Started!");

  matrix.begin();
  myNex.begin(9600);

  // Splash screen
  myNex.writeStr("page 0");
  myNex.writeStr("t1.txt", "ShackSwitch V1.3");
  delay(1500);

  myNex.writeStr("t1.txt", "Loading config...");
  loadConfig();
  RTC.begin();
  for (int i = 0; i < 4; i++) { pinMode(relayPins[i], OUTPUT); digitalWrite(relayPins[i], LOW); }

  myNex.writeStr("t1.txt", "Connecting WiFi...");
  connectToWiFi();
  server.begin();

  myNex.writeStr("t1.txt", "Syncing time...");
  syncTime();

  myNex.writeStr("page 0");
  for (int i = 0; i < 4; i++) {
    myNex.writeStr("b" + String(i + 1) + ".txt", myConfig.names[i]);
  }
  myNex.writeStr("t1.txt", WiFi.localIP().toString());
}

void loop() {
  myNex.NextionListen();

  // RSSI update every 10 seconds
  if (millis() - lastRSSIUpdate > 10000) {
    if (onMonitorPage || WiFi.status() == WL_CONNECTED) {
      updateStationMonitor();
    }
    lastRSSIUpdate = millis();
  }

  // Reset Safety Timeout
  if (resetConfirmed && (millis() - resetTimer > 5000)) {
    resetConfirmed = false;
    myNex.writeStr("tStatus.txt", "Reset Canceled");
  }

  // WiFi status and reconnect (non-blocking)
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      // Just reconnected — restore IP display
      myNex.writeStr("t1.txt", WiFi.localIP().toString());
      wifiWasConnected = true;
      Serial.println("WiFi reconnected: " + WiFi.localIP().toString());
    }
  } else if (!onConfigPage) {
    wifiWasConnected = false;
    if (millis() - lastWiFiCheck > 30000) {
      myNex.writeStr("t1.txt", "WiFi lost - retrying...");
      Serial.println("WiFi lost - retrying...");
      WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
      lastWiFiCheck = millis();
    } else {
      int secsLeft = 30 - ((millis() - lastWiFiCheck) / 1000);
      myNex.writeStr("t1.txt", "No WiFi - retry " + String(secsLeft) + "s");
    }
  }

  // RTC clock update every 25 seconds
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

  // Web Server
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    if (request.indexOf("GET /settings") != -1) {
      showSettingsPage(client);
    } 
    else if (request.indexOf("GET /rename") != -1) {
      int idPos = request.indexOf("id=") + 3;
      int rId = request.substring(idPos, idPos + 1).toInt();
      int namePos = request.indexOf("name=") + 5;
      int endPos = request.indexOf(" ", namePos);
      String nN = request.substring(namePos, endPos);
      nN.replace("+", " "); nN.replace("%20", " ");
      
      if (rId >= 1 && rId <= 4) {
        nN.toCharArray(myConfig.names[rId-1], 26);
        myConfig.configMagic = CONFIG_VERSION;
        EEPROM.put(0, myConfig);
        myNex.writeStr("b" + String(rId) + ".txt", nN);
      }
      client.println("HTTP/1.1 303 See Other\r\nLocation: /settings\r\n\r\n");
    }
    else if (request.indexOf("/on") != -1 || request.indexOf("/off") != -1) {
      for (int i = 1; i <= 4; i++) {
        if (request.indexOf("/" + String(i) + "/on") != -1)  controlRelay(i, HIGH); 
        if (request.indexOf("/" + String(i) + "/off") != -1) controlRelay(i, LOW);
      }
      client.println("HTTP/1.1 303 See Other\r\nLocation: /\r\n\r\n");
    }
    else {
      showMainPage(client);
    }
    client.stop();
  }
}
