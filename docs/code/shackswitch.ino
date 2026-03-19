/* 
 * VERSION 1.4 - G0JKN Antenna Switcher 
 * Integrated State Sync, Background WiFi Manager, and Factory Reset
 * Updated for dual-state image buttons (b1-b4) on Page 0
 * Antenna name labels: t3=b1, t4=b2, t5=b3, t6=b4
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

// --- CORE FUNCTIONS ---

void updateStationMonitor() {
  if (WiFi.status() == WL_CONNECTED) {
    currentRSSI = WiFi.RSSI();
    
    int signalQuality = map(currentRSSI, -100, -40, 0, 100);
    signalQuality = constrain(signalQuality, 0, 100);

    myNex.writeStr("tRSSI.txt", String(currentRSSI) + " dBm");
    myNex.writeNum("nSignal.val", signalQuality);
    myNex.writeStr("b0.txt", WiFi.localIP().toString());

    if (currentRSSI > -60) myNex.writeNum("nSignal.pco", 1024);
    else if (currentRSSI > -80) myNex.writeNum("nSignal.pco", 63488);
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

/*
 * FUNCTION: syncButtonStates
 * --------------------------
 * PURPOSE: Pushes the current relay state to all four dual-state buttons
 * on the Nextion. val=1 = active (pressed image), val=0 = grounded (released image).
 * Call this any time relay state changes, or on startup to initialise the display.
 */
void syncButtonStates() {
  for (int i = 0; i < 4; i++) {
    int val = (digitalRead(relayPins[i]) == HIGH) ? 1 : 0;
    myNex.writeNum("b" + String(i + 1) + ".val", val);
  }
}

/*
 * FUNCTION: syncAntennaNames
 * --------------------------
 * PURPOSE: Pushes all four antenna names from EEPROM config to the Nextion
 * text label components positioned above each dual-state button.
 * Layout: t3=b1, t4=b2, t5=b3, t6=b4
 * Call on startup and after any rename via the web interface.
 */
void syncAntennaNames() {
  for (int i = 0; i < 4; i++) {
    myNex.writeStr("t" + String(i + 3) + ".txt", myConfig.names[i]);
  }
}

/*
 * FUNCTION: controlRelay
 * ----------------------
 * PURPOSE: Sets a relay HIGH or LOW, then syncs all dual-state button images
 * and updates the antenna status display and LED matrix.
 *
 * NOTE: Dual-state buttons use .val (0 or 1) to switch between their two
 * images. .bco is not used as these buttons are image-based, not colour-based.
 */
void controlRelay(int targetIndex, bool state) {
  int idx = targetIndex - 1;

  if (state == HIGH) {
    // Only one relay active at a time — turn all others off
    currentActiveRelay = targetIndex;
    for (int i = 0; i < 4; i++) {
      digitalWrite(relayPins[i], (i == idx) ? HIGH : LOW);
    }
  } else {
    digitalWrite(relayPins[idx], LOW);
    if (currentActiveRelay == targetIndex) currentActiveRelay = 0;
  }

  // Sync all button images to match relay states
  syncButtonStates();

  updateAntennaStatus();
}

/* 
 * FUNCTION: updateAntennaStatus
 * ----------------------------
 * PURPOSE: Monitors 4 relay pins and updates the Nextion display and LED Matrix.
 * 
 * LOGIC FLOW:
 * 1. Scans relayPins array (Pins 2, 3, 4, 5).
 * 2. If a pin is HIGH, maps the array index (0-3) to Antenna ID (1-4).
 * 3. Updates Nextion text (tState.txt) and color (tState.pco).
 *    - Active Color: 63488 (Red)
 *    - Grounded Color: 1024 (Green)
 * 4. Calls updateMatrix() with the ID (1-4) or 0 if none are active.
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

  // Wait up to 15 seconds for connection rather than a blind delay
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection timed out.");
  }
  // IP is written to t1.txt after page 0 in setup() to avoid page reload wiping it
}

// --- WEB PAGES ---

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
  c.println("<h1>G0JKN 1.4 ANT SWITCH</h1>");

  // Render cards with IDs for JS to target
  for (int i = 0; i < 4; i++) {
    bool st = digitalRead(relayPins[i]);
    String cardClass = st ? " active" : "";
    c.print("<div class='card" + cardClass + "' id='card" + String(i+1) + "'>");
    c.print("<strong>" + String(myConfig.names[i]) + "</strong>");
    if(st) c.print("<a href='/" + String(i+1) + "/off' class='btn on' id='btn" + String(i+1) + "'>ACTIVE</a>");
    else    c.print("<a href='/" + String(i+1) + "/on'  class='btn off' id='btn" + String(i+1) + "'>GROUNDED</a>");
    c.print("</div>");
  }

  c.println("<br><a href='/settings' style='color:#666;'>[ SETTINGS ]</a>");

  // JavaScript polling — fetches /status every 5 seconds and updates cards in place
  c.println("<script>");
  c.println("function updateStatus(){");
  c.println("  fetch('/status').then(r=>r.json()).then(d=>{");
  c.println("    for(let i=1;i<=4;i++){");
  c.println("      const on=d['r'+i]===1;");
  c.println("      const card=document.getElementById('card'+i);");
  c.println("      const btn=document.getElementById('btn'+i);");
  c.println("      if(on){");
  c.println("        card.className='card active';");
  c.println("        btn.className='btn on';");
  c.println("        btn.textContent='ACTIVE';");
  c.println("        btn.href='/'+i+'/off';");
  c.println("      } else {");
  c.println("        card.className='card';");
  c.println("        btn.className='btn off';");
  c.println("        btn.textContent='GROUNDED';");
  c.println("        btn.href='/'+i+'/on';");
  c.println("      }");
  c.println("    }");
  c.println("  }).catch(()=>{});");  // silently ignore if Arduino is busy
  c.println("}");
  c.println("setInterval(updateStatus,5000);");
  c.println("</script>");
  c.println("</body></html>");
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
  while (!Serial) { ; }
  Serial.println("Debug Started!");

  matrix.begin();
  myNex.begin(9600);
  loadConfig();
  RTC.begin();

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  connectToWiFi();
  server.begin();
  syncTime();

  myNex.writeStr("page 0");

  // Dual-state buttons use .val not .txt — sync all to off (val=0) on startup
  // If you have separate text labels (t components) for antenna names, set them here instead
  syncButtonStates();
  syncAntennaNames();

  myNex.writeStr("t1.txt", WiFi.localIP().toString());
}

void loop() {
  myNex.NextionListen();

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

  // WiFi Reconnect (Non-blocking)
  if (WiFi.status() != WL_CONNECTED && !onConfigPage) {
    if (millis() - lastWiFiCheck > 30000) {
      WiFi.begin(myConfig.wifiSSID, myConfig.wifiPass);
      lastWiFiCheck = millis();
    }
    myNex.writeStr("t1.txt", "N O T   C O N N E C T E D");
  }

  // RTC Update (25s)
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

    if (request.indexOf("GET /status") != -1) {
      // JSON status endpoint — polled every 5s by the web page JavaScript
      String json = "{";
      for (int i = 0; i < 4; i++) {
        json += "\"r" + String(i+1) + "\":" + String(digitalRead(relayPins[i]));
        if (i < 3) json += ",";
      }
      json += "}";
      client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + json);
    }
    else if (request.indexOf("GET /settings") != -1) {
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
        // Push updated name to the corresponding Nextion label (t3=relay1, t4=relay2, etc.)
        myNex.writeStr("t" + String(rId + 2) + ".txt", nN);
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
