#include <Arduino_RouterBridge.h>
#include <Wire.h>

// G0JKN ShackSwitch v2.0 - STM32 Firmware

#define NUM_RELAYS 4
const int RELAY_PINS[] = {2, 3, 4, 5};
const int DIP_PINS[]   = {6, 7, 8, 9};

#define MCP23017_ADDR   0x20
#define MCP23017_ADDR2  0x21
#define MCP_IODIRA  0x00
#define MCP_IODIRB  0x01
#define MCP_GPIOA   0x12
#define MCP_GPIOB   0x13
#define MCP_GPPUA   0x0C
#define MCP_GPPUB   0x0D
#define KK1L_PORTS  6

uint8_t gpa_state = 0x00;
uint8_t gpb_state = 0x00;
bool mcp_found  = false;
bool mcp2_found = false;

void mcp_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(MCP23017_ADDR);
    Wire1.write(reg); Wire1.write(val);
    Wire1.endTransmission();
}
void mcp2_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(MCP23017_ADDR2);
    Wire1.write(reg); Wire1.write(val);
    Wire1.endTransmission();
}

void mcp_init() {
    Wire1.beginTransmission(MCP23017_ADDR);
    if (Wire1.endTransmission() == 0) {
        mcp_found = true;
        mcp_write(MCP_IODIRA, 0x00);
        mcp_write(MCP_IODIRB, 0xFF);
        mcp_write(MCP_GPPUB,  0xFF);
        mcp_write(MCP_GPIOA,  0x00);
        Monitor.println("MCP23017 #1 found at 0x20 - RLYT board ready");
    } else {
        mcp_found = false;
        Monitor.println("MCP23017 #1 NOT found at 0x20");
    }
    Wire1.beginTransmission(MCP23017_ADDR2);
    if (Wire1.endTransmission() == 0) {
        mcp2_found = true;
        mcp2_write(MCP_IODIRA, 0x00);
        mcp2_write(MCP_IODIRB, 0xFF);
        mcp2_write(MCP_GPPUB,  0xFF);
        mcp2_write(MCP_GPIOA,  0x00);
        Monitor.println("MCP23017 #2 found at 0x22 - RLYB board ready");
    } else {
        mcp2_found = false;
        Monitor.println("MCP23017 #2 NOT found at 0x22");
    }
}

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
bool kk1l_deselect_all() {
    if (!mcp_found) return false;
    gpa_state = 0x00; gpb_state = 0x00;
    mcp_write(MCP_GPIOA, gpa_state);
    if (mcp2_found) mcp2_write(MCP_GPIOA, gpb_state);
    Monitor.println("KK1L all ports deselected");
    return true;
}
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
String get_config() {
    int val = 0;
    for (int i = 0; i < 4; i++) {
        if (digitalRead(DIP_PINS[i]) == LOW) val |= (1 << i);
    }
    return String(val);
}
// Nextion serial bridge
static uint8_t nxt_buf[8];
static int     nxt_blen = 0;
static int     nxt_evt_page = -1;
static int     nxt_evt_comp = -1;

void nextion_poll_serial() {
    while (Serial.available()) {
        uint8_t b = Serial.read();
        nxt_buf[nxt_blen++] = b;

        // Custom button packet from HMI printh: 23 02 54 NN (NN = port 1-4)
        if (nxt_blen == 4 &&
            nxt_buf[0] == 0x23 &&
            nxt_buf[1] == 0x02 &&
            nxt_buf[2] == 0x54) {
            nxt_evt_page = 0;
            nxt_evt_comp = nxt_buf[3];
            nxt_blen = 0;
            continue;
        }

        // Standard Nextion response packets end with FF FF FF — consume and discard
        if (nxt_blen >= 3 &&
            nxt_buf[nxt_blen-1] == 0xFF &&
            nxt_buf[nxt_blen-2] == 0xFF &&
            nxt_buf[nxt_blen-3] == 0xFF) {
            if (nxt_buf[0] == 0x65 && nxt_blen == 7) {
                nxt_evt_page = nxt_buf[1];
                nxt_evt_comp = nxt_buf[2];
            }
            nxt_blen = 0;
            continue;
        }

        if (nxt_blen >= 8) nxt_blen = 0;
    }
}

String nextion_cmd(String cmd) {
    for (int i = 0; i < (int)cmd.length(); i++) Serial.write((uint8_t)cmd[i]);
    Serial.write(0xFF); Serial.write(0xFF); Serial.write(0xFF);
    return "ok";
}

String nextion_get_event() {
    if (nxt_evt_page < 0) return "";
    String evt = String(nxt_evt_page) + "," + String(nxt_evt_comp);
    nxt_evt_page = -1;
    nxt_evt_comp = -1;
    return evt;
}
void setup() {
    Bridge.begin(); Monitor.begin();Serial.begin(9600); Wire1.begin();
    for (int i = 0; i < NUM_RELAYS; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
    }
    for (int i = 0; i < 4; i++) pinMode(DIP_PINS[i], INPUT_PULLUP);
    mcp_init();
    Bridge.provide("relay_on",          relay_on);
    Bridge.provide("relay_off",         relay_off);
    Bridge.provide("get_status",        get_status);
    Bridge.provide("kk1l_select_a",     kk1l_select_a);
    Bridge.provide("kk1l_select_b",     kk1l_select_b);
    Bridge.provide("kk1l_deselect",     kk1l_deselect);
    Bridge.provide("kk1l_deselect_all", kk1l_deselect_all);
    Bridge.provide("kk1l_status",       kk1l_status);
    Bridge.provide("get_config",        get_config);
    Bridge.provide("nextion_cmd",       nextion_cmd);
    Bridge.provide("nextion_get_event", nextion_get_event);
    Monitor.println("ShackSwitch Q v2.0 ready");
    Monitor.println("MCP1 RLYT: " + String(mcp_found  ? "online" : "offline"));
    Monitor.println("MCP2 RLYB: " + String(mcp2_found ? "online" : "offline"));
}
void loop() { Bridge.update(); 
            nextion_poll_serial();}
