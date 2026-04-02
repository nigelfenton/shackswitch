#include <Arduino_RouterBridge.h>

#define NUM_RELAYS 4
const int RELAY_PINS[] = {2, 3, 4, 5};

bool relay_on(int n) {
  if (n < 1 || n > NUM_RELAYS) return false;
  digitalWrite(RELAY_PINS[n-1], HIGH);
  Monitor.println("Relay ON: " + String(n));
  return true;
}

bool relay_off(int n) {
  if (n < 1 || n > NUM_RELAYS) return false;
  digitalWrite(RELAY_PINS[n-1], LOW);
  Monitor.println("Relay OFF: " + String(n));
  return true;
}

String get_status() {
  String s = "";
  for (int i = 0; i < NUM_RELAYS; i++) {
    s += String(digitalRead(RELAY_PINS[i]));
    if (i < NUM_RELAYS - 1) s += ",";
  }
  return s;
}

void setup() {
  Bridge.begin();
  Monitor.begin();
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }
  Bridge.provide("relay_on", relay_on);
  Bridge.provide("relay_off", relay_off);
  Bridge.provide("get_status", get_status);
  Monitor.println("ShackSwitch Q ready — 4 relays initialised");
}

void loop() {
  Bridge.update();
}
