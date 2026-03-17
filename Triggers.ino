
// --- NEXTION TRIGGERS ---

void trigger1() { controlRelay(1, !digitalRead(relayPins[0])); }
void trigger2() { controlRelay(2, !digitalRead(relayPins[1])); }
void trigger3() { controlRelay(3, !digitalRead(relayPins[2])); }
void trigger4() { controlRelay(4, !digitalRead(relayPins[3])); }

void trigger6() { // Manual Scan
  WiFi.disconnect(); delay(500);
  int n = WiFi.scanNetworks();
  for (int i=0; i<6; i++) myNex.writeStr("t"+String(i)+".txt", (i<n) ? WiFi.SSID(i) : "-");
}

void trigger7() { // Connect
  int idx = myNex.readNumber("n0.val");
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

void trigger8() { // Factory Reset
  if (!resetConfirmed) {
    resetConfirmed = true; resetTimer = millis();
    myNex.writeStr("tStatus.txt", "TAP AGAIN!");
  } else {
    for (int i = 0; i < sizeof(myConfig); i++) EEPROM.write(i, 0xFF);
    delay(1000); NVIC_SystemReset();
  }
}

void trigger9() { onConfigPage = true; trigger6(); } // Auto Scan on Page 2
void trigger11() { onMonitorPage = true; updateStationMonitor(); } // Entering Page 3
void trigger12() { onMonitorPage = false; } // Leaving Page 3 (put on the 'Back' button)
