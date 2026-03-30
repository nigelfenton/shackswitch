// ============================================================
//  G0JKN ShackSwitch — triggers.ino
//  Nextion trigger callback functions v1.5
//
//  Trigger number map:
//
//  INPUT 1 (bA buttons) — printh 23 02 54 01-08
//    trigger1  = bA1    printh 23 02 54 01
//    trigger2  = bA2    printh 23 02 54 02
//    trigger3  = bA3    printh 23 02 54 03
//    trigger4  = bA4    printh 23 02 54 04
//    trigger5  = bA5    printh 23 02 54 05
//    trigger6  = bA6    printh 23 02 54 06
//    trigger7  = bA7    printh 23 02 54 07
//    trigger8  = bA8    printh 23 02 54 08
//
//  INPUT 2 (bB buttons) — printh 23 02 54 11-18
//    trigger17 = bB1    printh 23 02 54 11
//    trigger18 = bB2    printh 23 02 54 12
//    trigger19 = bB3    printh 23 02 54 13
//    trigger20 = bB4    printh 23 02 54 14
//    trigger21 = bB5    printh 23 02 54 15
//    trigger22 = bB6    printh 23 02 54 16
//    trigger23 = bB7    printh 23 02 54 17
//    trigger24 = bB8    printh 23 02 54 18
//
//  CONTROL functions — printh 23 02 54 21-26
//    trigger33 = WiFi scan        printh 23 02 54 21
//    trigger34 = WiFi connect     printh 23 02 54 22
//    trigger35 = Factory reset    printh 23 02 54 23
//    trigger36 = Enter config     printh 23 02 54 24
//    trigger37 = Enter monitor    printh 23 02 54 25
//    trigger38 = Leave monitor    printh 23 02 54 26
//
//  Unused slots: trigger9-16, trigger25-32
// ============================================================


// ============================================================
//  INPUT 1 (bA) ANTENNA SELECTION
//  Toggle: press active antenna to deselect,
//          press inactive antenna to select
// ============================================================

void selectInputA(int ant) {
  if (portA.rxAntenna == ant) {
    // Deselect
    portA.rxAntenna    = 0;
    currentActiveRelay = 0;
    if (ant <= RELAY_COUNT) digitalWrite(relayPins[ant - 1], LOW);
  } else {
    // Select — ground all relays first, then fire target
    portA.rxAntenna    = ant;
    currentActiveRelay = ant;
    for (int i = 0; i < RELAY_COUNT; i++) {
      digitalWrite(relayPins[i], (i == ant - 1) ? HIGH : LOW);
    }
  }
  evaluateInterlock();
  syncButtonStates();
  syncAntennaNames();
  updateAntennaStatus();
  updateNextionBandDisplay();
  Serial.print(F("[bA] Input 1 -> ANT ")); Serial.println(ant);
}

void trigger1() { selectInputA(1); }
void trigger2() { selectInputA(2); }
void trigger3() { selectInputA(3); }
void trigger4() { selectInputA(4); }
void trigger5() { selectInputA(5); }
void trigger6() { selectInputA(6); }
void trigger7() { selectInputA(7); }
void trigger8() { selectInputA(8); }


// ============================================================
//  INPUT 2 (bB) ANTENNA SELECTION
//  Interlock prevents selecting same antenna as Input 1.
//  Physical relay not driven until KK1L board fitted.
// ============================================================

void selectInputB(int ant) {
  // Interlock check — can't use same antenna as Input 1
  if (portA.rxAntenna == ant) {
    Serial.print(F("[INTERLOCK] bB")); Serial.print(ant);
    Serial.println(F(" blocked — same as Input 1"));
    // Flash tSO2R to warn user
    myNex.writeStr("tSO2R.txt", "CONFLICT!");
    myNex.writeNum("tSO2R.pco", 63488);  // red
    delay(800);
    updateNextionBandDisplay();  // restore correct state
    return;
  }

  if (portB.rxAntenna == ant) {
    // Deselect
    portB.rxAntenna = 0;
  } else {
    // Select
    portB.rxAntenna = ant;
    // TODO: drive Port B relay bank when KK1L board fitted
  }

  evaluateInterlock();
  syncButtonStates();
  updateNextionBandDisplay();
  Serial.print(F("[bB] Input 2 -> ANT ")); Serial.println(ant);
}

void trigger17() { selectInputB(1); }
void trigger18() { selectInputB(2); }
void trigger19() { selectInputB(3); }
void trigger20() { selectInputB(4); }
void trigger21() { selectInputB(5); }
void trigger22() { selectInputB(6); }
void trigger23() { selectInputB(7); }
void trigger24() { selectInputB(8); }


// ============================================================
//  CONTROL FUNCTIONS
// ============================================================

/*
 * trigger33 — Manual WiFi network scan
 * Disconnects, scans, writes up to 6 SSIDs to Nextion t0-t5
 */
void trigger33() {
  WiFi.disconnect();
  delay(500);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < 6; i++)
    myNex.writeStr("t" + String(i) + ".txt", (i < n) ? WiFi.SSID(i) : "-");
}

/*
 * trigger34 — Connect to selected WiFi network
 * Reads selected index from n0, password from tPass.
 * Saves to EEPROM and reconnects. Returns to correct page
 * for current port mode.
 */
void trigger34() {
  int    idx  = myNex.readNumber("n0.val");
  String pass = myNex.readStr("tPass.txt");
  if (WiFi.SSID(idx) != nullptr) {
    strncpy(myConfig.wifiSSID, WiFi.SSID(idx), 33);
    pass.toCharArray(myConfig.wifiPass, 64);
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
    onConfigPage = false;
    connectToWiFi();
    switch (myConfig.portMode) {
      case 1:  myNex.writeStr("page 1"); break;
      case 2:  myNex.writeStr("page 2"); break;
      default: myNex.writeStr("page 0"); break;
    }
  }
}

/*
 * trigger35 — Factory reset (double-tap, 5-second window)
 * First tap: arms reset, shows TAP AGAIN! warning.
 * Second tap within 5 seconds: wipes EEPROM and reboots.
 * WARNING: clears all antenna names and WiFi credentials.
 */
void trigger35() {
  if (!resetConfirmed) {
    resetConfirmed = true;
    resetTimer     = millis();
    myNex.writeStr("tStatus.txt", "TAP AGAIN!");
  } else {
    for (int i = 0; i < (int)sizeof(myConfig); i++) EEPROM.write(i, 0xFF);
    delay(1000);
    NVIC_SystemReset();
  }
}

/*
 * trigger36 — Enter WiFi config page
 * Sets onConfigPage flag and auto-scans on entry.
 */
void trigger36() {
  onConfigPage = true;
  trigger33();   // auto-scan on entry
}

/*
 * trigger37 — Enter station monitor page
 * Sets onMonitorPage flag and pushes fresh RSSI update.
 */
void trigger37() {
  onMonitorPage = true;
  updateStationMonitor();
}

/*
 * trigger38 — Leave station monitor page
 * Clears onMonitorPage flag. Place on Back button.
 */
void trigger38() {
  onMonitorPage = false;
}
/*

---

**Nextion editor — printh commands to enter for every button:**

**All pages — bA buttons (Touch Release Event):**
```
bA1: printh 23 02 54 01
bA2: printh 23 02 54 02
bA3: printh 23 02 54 03
bA4: printh 23 02 54 04
bA5: printh 23 02 54 05  (pages 1 and 2 only)
bA6: printh 23 02 54 06  (pages 1 and 2 only)
bA7: printh 23 02 54 07  (page 2 only)
bA8: printh 23 02 54 08  (page 2 only)
```

**All pages — bB buttons (Touch Release Event):**
```
bB1: printh 23 02 54 11
bB2: printh 23 02 54 12
bB3: printh 23 02 54 13
bB4: printh 23 02 54 14
bB5: printh 23 02 54 15  (pages 1 and 2 only)
bB6: printh 23 02 54 16  (pages 1 and 2 only)
bB7: printh 23 02 54 17  (page 2 only)
bB8: printh 23 02 54 18  (page 2 only)
```

**Control buttons (config page, monitor page):**
```
WiFi scan button:     printh 23 02 54 21
WiFi connect button:  printh 23 02 54 22
Factory reset button: printh 23 02 54 23
Enter config button:  printh 23 02 54 24
Enter monitor button: printh 23 02 54 25
Leave monitor button: printh 23 02 54 26

*/