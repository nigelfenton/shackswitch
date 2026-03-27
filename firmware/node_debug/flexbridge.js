// flexbridge.js — FlexRadio → Antenna Switch Bridge (fixed)
//
// Connects to the Flex-6700 on TCP port 4992 and listens for
// frequency/band changes. When the band changes, sends real
// commands to the antenna switch via the WebSocket bridge.
//
// Usage:
//   node flexbridge.js [flex-ip] [switch-ip]
//
// Example:
//   node flexbridge.js 10.0.0.250 10.0.0.163
//
// Requires bridge.js to be running:
//   node bridge.js 10.0.0.163
//
// Architecture:
//   Flex-6700 (TCP:4992) → flexbridge.js → bridge.js (WS:9009) → Arduino

const net       = require('net');
const WebSocket = require('ws');

const FLEX_IP   = process.argv[2] || '10.0.0.250';
const FLEX_PORT = 4992;

const SWITCH_IP = process.argv[3] || '10.0.0.163';
const SWITCH_WS = `ws://${SWITCH_IP}:9009`;

// ── Band definitions (corrected) ─────────────────────────────
const BANDS = [
  { id:  1, name: '160m', start:  1.800, stop:  2.000 },
  { id:  2, name:  '80m', start:  3.500, stop:  4.000 },
  { id:  3, name:  '60m', start:  5.330, stop:  5.407 },
  { id:  4, name:  '40m', start:  7.000, stop:  7.300 },
  { id:  5, name:  '30m', start: 10.100, stop: 10.160 },
  { id:  6, name:  '20m', start: 14.000, stop: 14.350 },
  { id:  7, name:  '17m', start: 18.068, stop: 18.168 },
  { id:  8, name:  '15m', start: 21.000, stop: 21.450 },
  { id:  9, name:  '12m', start: 24.890, stop: 24.990 },
  { id: 10, name:  '10m', start: 28.000, stop: 29.700 },
  { id: 11, name:   '6m', start: 50.000, stop: 54.000 },
];

function bandForFreq(freqMhz) {
  return BANDS.find(b => freqMhz >= b.start && freqMhz <= b.stop) || null;
}

// ── Band → antenna mapping ────────────────────────────────────
// Antenna 1 = 160m, 2 = 80m, 3 = 60m, 4 = 40m,
// 5 = 20m, 6 = 15m, 7 = 10m/6m, 8 = Dummy Load (manual only)
const bandAntenna = {
  1: {   // Radio A
     1: 1,   // 160m → Antenna 1
     2: 2,   // 80m  → Antenna 2
     3: 3,   // 60m  → Antenna 3
     4: 4,   // 40m  → Antenna 4
     5: 4,   // 30m  → Antenna 4 (40m antenna covers 30m)
     6: 5,   // 20m  → Antenna 5
     7: 5,   // 17m  → Antenna 5
     8: 6,   // 15m  → Antenna 6
     9: 6,   // 12m  → Antenna 6
    10: 7,   // 10m  → Antenna 7
    11: 7,   // 6m   → Antenna 7
  },
  2: {   // Radio B — same mapping
     1: 1,
     2: 2,
     3: 3,
     4: 4,
     5: 4,
     6: 5,
     7: 5,
     8: 6,
     9: 6,
    10: 7,
    11: 7,
  },
};

// ── State ─────────────────────────────────────────────────────
const slices    = {};
let switchSeq   = 10;
let flexSeq     = 1;
let flexLineBuffer = '';
let switchReady = false;
let flexReady   = false;

// ── Banner ────────────────────────────────────────────────────
console.log('');
console.log('╔══════════════════════════════════════════════╗');
console.log('║   FlexRadio → Antenna Switch Bridge  v2     ║');
console.log('╚══════════════════════════════════════════════╝');
console.log(`Flex-6700 : ${FLEX_IP}:${FLEX_PORT}`);
console.log(`Switch WS : ${SWITCH_WS}`);
console.log('');

// ── WebSocket to antenna switch ───────────────────────────────
let switchWs = null;

function connectSwitch() {
  console.log(`[Switch] Connecting to bridge at ${SWITCH_WS}...`);
  switchWs = new WebSocket(SWITCH_WS);

  switchWs.on('open', () => {
    switchReady = true;
    console.log('[Switch] Connected ✓');
    checkReady();
  });

  switchWs.on('close', () => {
    switchReady = false;
    console.log('[Switch] Disconnected — retrying in 5s...');
    setTimeout(connectSwitch, 5000);
  });

  switchWs.on('error', (err) => {
    console.error('[Switch] Error:', err.message);
    if (err.code === 'ECONNREFUSED') {
      console.error('[Switch] Is bridge.js running? → node bridge.js ' + SWITCH_IP);
    }
  });
}

function sendSwitchCommand(cmd) {
  if (!switchWs || switchWs.readyState !== WebSocket.OPEN) {
    console.warn('[Switch] Not connected — dropped:', cmd);
    return;
  }
  const line = `C${switchSeq}|${cmd}\r\n`;
  console.log(`[Switch] → ${line.trim()}`);
  switchWs.send(line);
  switchSeq++;
}

// ── TCP connection to Flex ────────────────────────────────────
let flexClient = null;

function connectFlex() {
  console.log(`[Flex]   Connecting to ${FLEX_IP}:${FLEX_PORT}...`);
  flexClient = net.createConnection(FLEX_PORT, FLEX_IP, () => {
    flexReady = true;
    console.log('[Flex]   Connected ✓');
    checkReady();
    sendFlexCommand('sub slice all');
    sendFlexCommand('sub tx all');
    sendFlexCommand('slice list');
  });

  flexClient.on('data', (data) => {
    flexLineBuffer += data.toString();
    const lines = flexLineBuffer.split('\n');
    flexLineBuffer = lines.pop();
    lines.forEach(line => {
      line = line.trim();
      if (line) processFlexLine(line);
    });
  });

  flexClient.on('close', () => {
    flexReady = false;
    console.log('[Flex]   Disconnected — retrying in 5s...');
    setTimeout(connectFlex, 5000);
  });

  flexClient.on('error', (err) => {
    console.error('[Flex]   Error:', err.message);
  });
}

function sendFlexCommand(cmd) {
  if (!flexClient) return;
  const line = `C${flexSeq}|${cmd}\r\n`;
  flexClient.write(line);
  flexSeq++;
}

// ── Ready check ───────────────────────────────────────────────
function checkReady() {
  if (switchReady && flexReady) {
    console.log('');
    console.log('╔══════════════════════════════════════════════╗');
    console.log('║        Bridge is LIVE — both connected!     ║');
    console.log('╚══════════════════════════════════════════════╝');
    console.log('Tune Bigone and watch antenna selection happen automatically.\n');
  }
}

// ── Flex line processor ───────────────────────────────────────
function processFlexLine(line) {
  if (line.startsWith('V')) {
    console.log(`[Flex]   API version: ${line.slice(1)}`);
  } else if (line.startsWith('H')) {
    console.log(`[Flex]   Client handle: ${line.slice(1)}`);
  } else if (line.startsWith('S')) {
    const pipeIdx = line.indexOf('|');
    if (pipeIdx < 0) return;
    processFlexStatus(line.slice(pipeIdx + 1));
  }
}

// ── Flex status processor ─────────────────────────────────────
function processFlexStatus(body) {
  const parts  = body.split(' ');
  const type   = parts[0];
  const fields = parseKV(body);

  if (type === 'slice') {
    // Slice number is the SECOND word
    // e.g. "slice 0 RF_frequency=7.173 index_letter=A tx=1 rxant=ANT1 ..."
    //       "slice 1 RF_frequency=29.22 index_letter=B tx=0 rxant=ANT2 ..."
    const sliceId     = parseInt(parts[1] ?? '0');
    const freqStr     = fields.RF_frequency;
    const indexLetter = fields.index_letter || (sliceId === 0 ? 'A' : 'B');

    // Map: slice 0 → port 1 (Radio A), slice 1 → port 2 (Radio B)
    const portId = sliceId + 1;

    if (freqStr !== undefined) {
      handleFrequencyChange(portId, sliceId, indexLetter, parseFloat(freqStr));
    }

  } else if (type === 'interlock') {
    handleTxState(fields.state || '');
  }
}

// ── Frequency change handler ──────────────────────────────────
function handleFrequencyChange(portId, sliceId, letter, freqMhz) {
  const prev     = slices[sliceId];
  const band     = bandForFreq(freqMhz);
  const bandId   = band ? band.id   : 0;
  const bandName = band ? band.name : 'out of band';
  const prevBandId = prev ? prev.bandId : -1;

  // Only act on band changes
  if (bandId === prevBandId) return;

  const timestamp = new Date().toLocaleTimeString();
  console.log(`\n[${timestamp}] Slice ${sliceId} (${letter} / Radio ${portId === 1 ? 'A' : 'B'}): ${freqMhz.toFixed(6)} MHz → ${bandName}`);

  slices[sliceId] = { freqMhz, bandId, bandName, letter };

  if (bandId === 0) {
    console.log(`  ↳ Out of band — no antenna change`);
    return;
  }

  // Look up antenna for this port + band
  const antId = bandAntenna[portId]?.[bandId];
  if (!antId) {
    console.log(`  ↳ No antenna mapped for ${bandName} on port ${portId}`);
    return;
  }

  console.log(`  ↳ Selecting Antenna ${antId} for ${bandName} on Radio ${letter}`);

  // Send real commands to antenna switch
  sendSwitchCommand(`port set ${portId} rxant=${antId} txant=${antId} auto=1 band=${bandId}`);
  sendSwitchCommand(`interlock set radio${letter}=0 band=${bandId}`);
}

// ── TX state handler ──────────────────────────────────────────
function handleTxState(state) {
  const timestamp = new Date().toLocaleTimeString();
  const txBand = slices[0]?.bandId || 0;

  if (state === 'PTT_REQUESTED' || state === 'TRANSMITTING') {
    console.log(`\n[${timestamp}] ⚡ TX ACTIVE — locking SO2R interlock`);
    sendSwitchCommand(`interlock set radioA=1 band=${txBand}`);
  } else if (state === 'RECEIVE' || state === 'READY') {
    console.log(`\n[${timestamp}]  RX — releasing interlock`);
    sendSwitchCommand(`interlock set radioA=0 band=${txBand}`);
  }
}

// ── Key=value parser ──────────────────────────────────────────
function parseKV(text) {
  const result = {};
  const re = /(\S+?)=([^\s]*)/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    result[m[1]] = m[2];
  }
  return result;
}

// ── Start ─────────────────────────────────────────────────────
connectSwitch();
setTimeout(connectFlex, 1000);
