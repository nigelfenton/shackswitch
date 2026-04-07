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

// --- MCP23017 board 1 (0x20) — RLYT relay drivers + LEDs on Port A ---
// --- MCP23017 board 2 (0x21) — RLYB relay drivers + LEDs on Port A ---
#define MCP23017_ADDR   0x20
#define MCP23017_ADDR2  0x21
#define MCP_IODIRA  0x00
#define MCP_IODIRB  0x01
#define MCP_GPIOA   0x12
#define MCP_GPIOB   0x13
#define MCP_GPPUA   0x0C
#define MCP_GPPUB   0x0D

// KK1L port count
#define KK1L_PORTS 6

// gpa_state: bit n HIGH = port (n+1) connected to Input A (RLYT relay, MCP1 Port A)
// gpb_state: bit n HIGH = port (n+1) connected to Input B (RLYB relay, MCP2 Port A)
// Both LOW = port terminated to 50 ohm
uint8_t gpa_state = 0x00;
uint8_t gpb_state = 0x00;

bool mcp_found  = false;
bool mcp2_found = false;

// =============================================================
// MCP23017 helpers
// =============================================================

void mcp_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(MCP23017_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

void mcp2_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(MCP23017_ADDR2);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

void mcp_init() {
    // --- Board 1 (0x20) — RLYT drivers on Port A ---
    Wire1.beginTransmission(MCP23017_ADDR);
    if (Wire1.endTransmission() == 0) {
        mcp_found = true;
        mcp_write(MCP_IODIRA, 0x00);  // GPA all outputs (RLYT relay drivers)
        mcp_write(MCP_IODIRB, 0xFF);  // GPB all inputs (config/settings)
        mcp_write(MCP_GPPUB,  0xFF);  // pullups on GPB inputs
        mcp_write(MCP_GPIOA,  0x00);  // all RLYT LOW (all ports to Input B bus)
        Monitor.println("MCP23017 #1 found at 0x20 — RLYT board ready");
    } else {
        mcp_found = false;
        Monitor.println("MCP23017 #1 NOT found at 0x20");
    }

    // --- Board 2 (0x21) — RLYB drivers on Port A ---
    Wire1.beginTransmission(MCP23017_ADDR2);
    if (Wire1.endTransmission() == 0) {
        mcp2_found = true;
        mcp2_write(MCP_IODIRA, 0x00);  // GPA all outputs (RLYB relay drivers)
        mcp2_write(MCP_IODIRB, 0xFF);  // GPB all inputs (config/settings)
        mcp2_write(MCP_GPPUB,  0xFF);  // pullups on GPB inputs
        mcp2_write(MCP_GPIOA,  0x00);  // all RLYB LOW (all ports 50 ohm terminated)
        Monitor.println("MCP23017 #2 found at 0x21 — RLYB board ready");
    } else {
        mcp2_found = false;
        Monitor.println("MCP23017 #2 NOT found at 0x21");
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
// MCP1 GPA bit HIGH (RLYT energised = Input A)
// MCP2 GPA bit LOW  (RLYB de-energised = not Input B)
bool kk1l_select_a(int port) {
    if (!mcp_found) return false;
    if (port < 1 || port > KK1L_PORTS) return false;
    int bit = port - 1;
    gpa_state |=  (1 << bit);
    gpb_state &= ~(1 << bit);
    mcp_write(MCP_GPIOA, gpa_state);
    if (mcp2_found) mcp2_write(MCP_GPIOA, gpb_state);
    Monitor.println("KK1L port " + String(port) + " -> Input A");
    return true;
}

// Connect port n (1-6) to Input B
// MCP1 GPA bit LOW  (RLYT de-energised = not Input A)
// MCP2 GPA bit HIGH (RLYB energised = Input B active)
bool kk1l_select_b(int port) {
    if (!mcp_found) return false;
    if (port < 1 || port > KK1L_PORTS) return false;
    int bit = port - 1;
    gpa_state &= ~(1 << bit);
    gpb_state |=  (1 << bit);
    mcp_write(MCP_GPIOA, gpa_state);
    if (mcp2_found) mcp2_write(MCP_GPIOA, gpb_state);
    Monitor.println("KK1L port " + String(port) + " -> Input B");
    return true;
}

// Deselect port n — 50 ohm termination
// Both RLYT and RLYB LOW
bool kk1l_deselect(int port) {
    if (!mcp_found) return false;
    if (port < 1 || port > KK1L_PORTS) return false;
    int bit = port - 1;
    gpa_state &= ~(1 << bit);
    gpb_state &= ~(1 << bit);
    mcp_write(MCP_GPIOA, gpa_state);
    if (mcp2_found) mcp2_write(MCP_GPIOA, gpb_state);
    Monitor.println("KK1L port " + String(port) + " deselected (50 ohm)");
    return true;
}

// Deselect all ports — safe state
bool kk1l_deselect_all() {
    if (!mcp_found) return false;
    gpa_state = 0x00;
    gpb_state = 0x00;
    mcp_write(MCP_GPIOA, gpa_state);
    if (mcp2_found) mcp2_write(MCP_GPIOA, gpb_state);
    Monitor.println("KK1L all ports deselected");
    return true;
}

// Return status string: "A,B,0,0,B,0"
// A = Input A, B = Input B, 0 = 50 ohm terminated
String kk1l_status() {
    if (!mcp_found) return "unavailable";
    String s = "";
    for (int i = 0; i < KK1L_PORTS; i++) {
        bool rlyt = (gpa_state >> i) & 1;
        bool rlyb = (gpb_state >> i) & 1;
        if      (rlyt) s += "A";
        else if (rlyb) s += "B";
        else           s += "0";
        if (i < KK1L_PORTS - 1) s += ",";
    }
    return s;
}

// Read DIP switches D6-D9, return 4-bit value (0-15)
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
    Wire1.begin();

    // Original relay shield (D2-D5)
    for (int i = 0; i < NUM_RELAYS; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
    }

    // DIP switch inputs (D6-D9)
    for (int i = 0; i < 4; i++) {
        pinMode(DIP_PINS[i], INPUT_PULLUP);
    }

    // KK1L MCP23017 boards
    mcp_init();

    // Register Bridge RPC methods
    Bridge.provide("relay_on",          relay_on);
    Bridge.provide("relay_off",         relay_off);
    Bridge.provide("get_status",        get_status);
    Bridge.provide("kk1l_select_a",     kk1l_select_a);
    Bridge.provide("kk1l_select_b",     kk1l_select_b);
    Bridge.provide("kk1l_deselect",     kk1l_deselect);
    Bridge.provide("kk1l_deselect_all", kk1l_deselect_all);
    Bridge.provide("kk1l_status",       kk1l_status);
    Bridge.provide("get_config",        get_config);

    Monitor.println("ShackSwitch Q v2.0 ready");
    Monitor.println("MCP1 RLYT: " + String(mcp_found  ? "online" : "offline"));
    Monitor.println("MCP2 RLYB: " + String(mcp2_found ? "online" : "offline"));
}

void loop() {
    Bridge.update();
}
