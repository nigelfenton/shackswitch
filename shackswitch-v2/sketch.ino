#include <Arduino_RouterBridge.h>
#include <Wire.h>

// =============================================================
// G0JKN ShackSwitch v2.0 — STM32 Firmware
// Arduino Uno Q (STM32U585 side)
// =============================================================

// --- Original 4-relay shield (v1.5 backward compat, D2-D5) ---
#define NUM_RELAYS 4
const int RELAY_PINS[] = {2, 3, 4, 5};

// --- DIP switch config inputs (D6-D9, active LOW with pullup) ---
const int DIP_PINS[] = {6, 7, 8, 9};

// --- MCP23017 KK1L board (I2C address 0x20) ---
#define MCP23017_ADDR 0x20
#define MCP_IODIRA    0x00   // GPA direction register
#define MCP_IODIRB    0x01   // GPB direction register
#define MCP_GPIOA     0x12   // GPA output register
#define MCP_GPIOB     0x13   // GPB output register
#define MCP_GPPUA     0x0C   // GPA pullup register
#define MCP_GPPUB     0x0D   // GPB pullup register

// KK1L port count
#define KK1L_PORTS 6

// GPA0-5 = RLYT0-5 (HIGH = Input A, LOW = Input B)
// GPB0-5 = RLYB0-5 (HIGH = port B active, LOW = 50 ohm terminated)
uint8_t gpa_state = 0x00;
uint8_t gpb_state = 0x00;

bool mcp_found = false;

// =============================================================
// MCP23017 helpers
// =============================================================

void mcp_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MCP23017_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void mcp_init() {
    Wire.beginTransmission(MCP23017_ADDR);
  if (Wire.endTransmission() == 0) {
    mcp_found = true;
    mcp_write(MCP_IODIRA, 0x00);  // GPA all outputs
    mcp_write(MCP_IODIRB, 0xC0);  // GPB0-5 outputs, GPB6-7 inputs
    mcp_write(MCP_GPPUB,  0xC0);  // pullups on GPB6-7 inputs
    mcp_write(MCP_GPIOA,  0x00);  // all GPA LOW (all ports to B bus, B relays off = 50 ohm)
    mcp_write(MCP_GPIOB,  0x00);  // all GPB LOW
    Monitor.println("MCP23017 found at 0x20 — KK1L board ready");
  } else {
    mcp_found = false;
    Monitor.println("MCP23017 NOT found — KK1L board unavailable");
  }
}

// =============================================================
// Original relay RPC (v1.5 compat, D2-D5)
// =============================================================

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

// =============================================================
// KK1L RPC methods
// =============================================================

// Connect port n (1-6) to Input A
// RLYT_n HIGH, RLYB_n LOW
bool kk1l_select_a(int port) {
    if (!mcp_found) return false;
  if (port < 1 || port > KK1L_PORTS) return false;
  int bit = port - 1;
  gpa_state |=  (1 << bit);   // RLYT high = Input A
  gpb_state &= ~(1 << bit);   // RLYB low  = not Input B
  mcp_write(MCP_GPIOA, gpa_state);
  mcp_write(MCP_GPIOB, gpb_state);
  Monitor.println("KK1L port " + String(port) + " -> Input A");
  return true;
}

// Connect port n (1-6) to Input B
// RLYT_n LOW, RLYB_n HIGH
bool kk1l_select_b(int port) {
    if (!mcp_found) return false;
  if (port < 1 || port > KK1L_PORTS) return false;
  int bit = port - 1;
  gpa_state &= ~(1 << bit);   // RLYT low  = Input B
  gpb_state |=  (1 << bit);   // RLYB high = Input B active
  mcp_write(MCP_GPIOA, gpa_state);
  mcp_write(MCP_GPIOB, gpb_state);
  Monitor.println("KK1L port " + String(port) + " -> Input B");
  return true;
}

// Deselect port n — 50 ohm termination
// RLYT_n LOW, RLYB_n LOW
bool kk1l_deselect(int port) {
    if (!mcp_found) return false;
  if (port < 1 || port > KK1L_PORTS) return false;
  int bit = port - 1;
  gpa_state &= ~(1 << bit);
  gpb_state &= ~(1 << bit);
  mcp_write(MCP_GPIOA, gpa_state);
  mcp_write(MCP_GPIOB, gpb_state);
  Monitor.println("KK1L port " + String(port) + " deselected (50 ohm)");
  return true;
}

// Deselect all ports — safe state
bool kk1l_deselect_all() {
    if (!mcp_found) return false;
  gpa_state = 0x00;
  gpb_state = 0x00;
  mcp_write(MCP_GPIOA, gpa_state);
  mcp_write(MCP_GPIOB, gpb_state);
  Monitor.println("KK1L all ports deselected");
  return true;
}

// Return status string: "A,B,0,0,B,0" — one entry per port
// A = connected to Input A, B = connected to Input B, 0 = terminated
String kk1l_status() {
    if (!mcp_found) return "unavailable";
  String s = "";
  for (int i = 0; i < KK1L_PORTS; i++) {
    bool rlyt = (gpa_state >> i) & 1;
    bool rlyb = (gpb_state >> i) & 1;
    if (rlyt)      s += "A";
    else if (rlyb) s += "B";
    else           s += "0";
    if (i < KK1L_PORTS - 1) s += ",";
  }
  return s;
}

// Read DIP switches D6-D9, return 4-bit value (0-15)
// Pins are INPUT_PULLUP so LOW = switch closed = 1
String get_config() {
    int val = 0;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(DIP_PINS[i]) == LOW) {
      val |= (1 << i);
    }
  }
  return String(val);
}

// =============================================================
// Setup & loop
// =============================================================

void setup() {
    Bridge.begin();
  Monitor.begin();
  Wire.begin();

  // Original relay shield
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }

  // DIP switch inputs with pullup
  for (int i = 0; i < 4; i++) {
    pinMode(DIP_PINS[i], INPUT_PULLUP);
  }

  // KK1L MCP23017
  mcp_init();

  // Register Bridge RPC methods
  Bridge.provide("relay_on",        relay_on);
  Bridge.provide("relay_off",       relay_off);
  Bridge.provide("get_status",      get_status);
  Bridge.provide("kk1l_select_a",   kk1l_select_a);
  Bridge.provide("kk1l_select_b",   kk1l_select_b);
  Bridge.provide("kk1l_deselect",   kk1l_deselect);
  Bridge.provide("kk1l_deselect_all", kk1l_deselect_all);
  Bridge.provide("kk1l_status",     kk1l_status);
  Bridge.provide("get_config",      get_config);

  Monitor.println("ShackSwitch Q v2.0 ready");
  Monitor.println("KK1L: " + String(mcp_found ? "online" : "offline"));
}

void loop() {
    Bridge.update();
}
