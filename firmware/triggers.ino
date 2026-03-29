/*
 * ============================================================
 *  G0JKN ShackSwitch — triggers.ino
 *  Nextion trigger callback functions
 *
 *  The EasyNextionLibrary calls trigger1() through trigger12()
 *  in response to Nextion touch events. Each Nextion button
 *  is configured in the HMI to send a printNum command on
 *  press, which the library maps to the corresponding
 *  trigger function here.
 *
 *  Trigger assignments:
 *    trigger1()  — Antenna 1 button (b1) pressed
 *    trigger2()  — Antenna 2 button (b2) pressed
 *    trigger3()  — Antenna 3 button (b3) pressed
 *    trigger4()  — Antenna 4 button (b4) pressed
 *    trigger6()  — WiFi manual scan (config page)
 *    trigger7()  — WiFi connect to selected network (config page)
 *    trigger8()  — Factory reset (double-tap, config page)
 *    trigger9()  — Enter config page (auto-scans on entry)
 *    trigger11() — Enter monitor page
 *    trigger12() — Leave monitor page
 *
 *  Note: trigger5() and trigger10() are intentionally unused.
 * ============================================================
 */


// ============================================================
//  ANTENNA SELECTION TRIGGERS
//  Each trigger toggles the corresponding relay. Because
//  controlRelay() enforces the single-antenna rule, pressing
//  an active button will ground it; pressing an inactive
//  button will activate it and ground all others.
// ============================================================

void trigger1() { controlRelay(1, !digitalRead(relayPins[0])); }
void trigger2() { controlRelay(2, !digitalRead(relayPins[1])); }
void trigger3() { controlRelay(3, !digitalRead(relayPins[2])); }
void trigger4() { controlRelay(4, !digitalRead(relayPins[3])); }


// ============================================================
//  WIFI CONFIG TRIGGERS
// ============================================================

/*
 * trigger6 — Manual WiFi network scan
 * ------------------------------------
 * Disconnects from current network, scans for available
 * SSIDs, and writes up to 6 results to Nextion text
 * components t0-t5 on the config page.
 * Unused slots are cleared to "-".
 */
void trigger6() {
  WiFi.disconnect();
  delay(500);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < 6; i++) {
    myNex.writeStr("t" + String(i) + ".txt", (i < n) ? WiFi.SSID(i) : "-");
  }
}

/*
 * trigger7 — Connect to selected WiFi network
 * --------------------------------------------
 * Reads the selected network index from Nextion numeric
 * component n0 and the password from tPass. Saves credentials
 * to EEPROM and attempts connection. Returns to page 0.
 */
void trigger7() {
  int    idx  = myNex.readNumber("n0.val");
  String pass = myNex.readStr("tPass.txt");

  if (WiFi.SSID(idx) != nullptr) {
    strncpy(myConfig.wifiSSID, WiFi.SSID(idx), 33);
    pass.toCharArray(myConfig.wifiPass, 64);
    myConfig.configMagic = CONFIG_VERSION;
    EEPROM.put(0, myConfig);
    onConfigPage = false;
    connectToWiFi();
    myNex.writeStr("page 0");
  }
}

/*
 * trigger8 — Factory reset (double-tap with 5-second window)
 * -----------------------------------------------------------
 * First tap: sets resetConfirmed flag, starts 5-second
 *            timeout, shows "TAP AGAIN!" on Nextion.
 * Second tap (within 5 seconds): clears EEPROM and reboots.
 *
 * If the second tap does not arrive within 5 seconds, the
 * main loop clears resetConfirmed and shows "Reset Canceled".
 *
 * WARNING: This erases all antenna names and WiFi credentials.
 */
void trigger8() {
  if (!resetConfirmed) {
    // First tap — arm the reset, start safety timer
    resetConfirmed = true;
    resetTimer     = millis();
    myNex.writeStr("tStatus.txt", "TAP AGAIN!");
  } else {
    // Second tap within timeout — perform factory reset
    for (int i = 0; i < (int)sizeof(myConfig); i++) EEPROM.write(i, 0xFF);
    delay(1000);
    NVIC_SystemReset();
  }
}

/*
 * trigger9 — Enter WiFi config page
 * -----------------------------------
 * Sets the onConfigPage flag (suppresses background WiFi
 * reconnect attempts during config) and immediately triggers
 * a network scan so the list is populated on page entry.
 */
void trigger9() {
  onConfigPage = true;
  trigger6();   // Auto-scan on page entry
}


// ============================================================
//  MONITOR PAGE TRIGGERS
// ============================================================

/*
 * trigger11 — Enter station monitor page
 * ----------------------------------------
 * Sets the onMonitorPage flag and immediately pushes a
 * fresh RSSI and signal quality update to the display.
 */
void trigger11() {
  onMonitorPage = true;
  updateStationMonitor();
}

/*
 * trigger12 — Leave station monitor page
 * ----------------------------------------
 * Clears the onMonitorPage flag. Place this trigger on the
 * Back button of the monitor page.
 */
void trigger12() {
  onMonitorPage = false;
}
