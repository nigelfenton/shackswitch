// flexconnect.js — FlexRadio TCP API Listener
//
// Connects to the Flex-6700 on TCP port 4992 and subscribes
// to slice frequency updates. Prints band changes in real time.
//
// Usage:
//   node flexconnect.js [flex-ip]
//
// Example:
//   node flexconnect.js 10.0.0.250

const net = require('net');
const dgram = require('dgram');

const FLEX_IP   = process.argv[2] || '10.0.0.250';
const FLEX_PORT = 4992;   // FlexRadio TCP control port

let seq = 1;          // command sequence number
let lineBuffer = '';  // accumulate incoming data into lines

// ── Band lookup ───────────────────────────────────────────────
const BANDS = [
  { id:  1, name: '160m', start:  1.800, stop:  2.000 },
  { id:  2, name:  '80m', start:  3.500, stop:  4.000 },
  { id:  3, name:  '60m', start:  5.330, stop:  5.407 },
  { id:  4, name:  '40m', start:  7.000, stop:  7.300 },
  { id:  5, name:  '30m', start: 10.100, stop: 10.160 },
  { id:  5, name:  '30m', start: 10.100, stop: 10.150 },
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

// ── Slice state ───────────────────────────────────────────────
// Tracks current frequency and band for each slice (0-7)
const slices = {};

function updateSlice(sliceId, freqMhz) {
  const prev = slices[sliceId];
  const band = bandForFreq(freqMhz);
  const bandName = band ? band.name : 'out of band';

  // Only print if frequency or band changed
  if (!prev || prev.freq !== freqMhz) {
    const timestamp = new Date().toLocaleTimeString();
    const changed = prev && bandForFreq(prev.freq)?.name !== bandName;

    console.log(`[${timestamp}] Slice ${sliceId}: ${freqMhz.toFixed(6)} MHz  →  ${bandName}${changed ? '  *** BAND CHANGE ***' : ''}`);

    if (changed) {
      // This is where we would send a command to the antenna switch!
      console.log(`  ↳ Antenna switch: select antenna for ${bandName} on Radio A`);
      console.log(`  ↳ Would send: C${seq}|port set 1 auto=1 band=${band?.id || 0}`);
    }
  }

  slices[sliceId] = { freq: freqMhz, band: bandName };
}

// ── TCP connection ────────────────────────────────────────────
console.log('');
console.log('╔════════════════════════════════════════╗');
console.log('║   FlexRadio TCP Frequency Listener     ║');
console.log('╚════════════════════════════════════════╝');
console.log(`Connecting to ${FLEX_IP}:${FLEX_PORT}...`);
console.log('');

const client = net.createConnection(FLEX_PORT, FLEX_IP, () => {
  console.log('Connected to Flex-6700!\n');
  // After connecting, subscribe to slice status updates
  // FlexRadio API: C<seq>|<command>
  sendCommand('sub slice all');
  sendCommand('sub tx all');
  sendCommand('slice list');
  sendCommand('slice get 0');
  sendCommand('slice get 1');
});

client.on('data', (data) => {
  // Accumulate data into lines
  lineBuffer += data.toString();
  const lines = lineBuffer.split('\n');
  // Keep the last incomplete line in the buffer
  lineBuffer = lines.pop();
  lines.forEach(line => {
    line = line.trim();
    if (line) processLine(line);
  });
});

client.on('close', () => {
  console.log('\nConnection closed.');
});

client.on('error', (err) => {
  console.error('Connection error:', err.message);
  if (err.code === 'ECONNREFUSED') {
    console.error(`Could not connect to ${FLEX_IP}:${FLEX_PORT}`);
    console.error('Make sure SmartSDR is running and the Flex is powered on.');
  }
});

// ── Command sender ────────────────────────────────────────────
function sendCommand(cmd) {
  const line = `C${seq}|${cmd}\r\n`;
  client.write(line);
  console.log(`→ ${line.trim()}`);
  seq++;
}

// ── Line processor ────────────────────────────────────────────
function processLine(line) {
  // FlexRadio responses:
  //   V<version>                     — version handshake on connect
  //   H<handle>                      — client handle assigned by radio
  //   M<id>|<message>                — log/message from radio
  //   R<seq>|<code>|<body>           — response to our command
  //   S<handle>|<object> key=val ... — async status update

  if (line.startsWith('V')) {
    console.log(`← Radio API version: ${line.slice(1)}`);

  } else if (line.startsWith('H')) {
    console.log(`← Client handle: ${line.slice(1)}`);

  } else if (line.startsWith('M')) {
    // Log messages from radio — usually informational
    const msg = line.slice(line.indexOf('|') + 1);
    console.log(`← Radio message: ${msg}`);

  } else if (line.startsWith('R')) {
    // Response to one of our commands
    const parts = line.slice(1).split('|');
    const respSeq  = parts[0];
    const respCode = parts[1];
    const respBody = parts.slice(2).join('|');
    if (respCode !== '00000000') {
      console.log(`← Response seq=${respSeq} code=${respCode} ${respBody}`);
    }

  } else if (line.startsWith('S')) {
    // Async status push — this is where frequency updates come from
    const pipeIdx = line.indexOf('|');
    if (pipeIdx < 0) return;
    const body = line.slice(pipeIdx + 1);
    processStatus(body);

  } else {
    // Unknown line — print it anyway so we can learn from it
    console.log(`← ${line}`);
  }
}

// ── Status processor ──────────────────────────────────────────
function processStatus(body) {
  const fields = parseKV(body);
  const type   = body.split(' ')[0];

  // Slice status update — contains RF_frequency
  if (type === 'slice') {
    const sliceId = fields.index !== undefined ? parseInt(fields.index) : 0;
    const freqStr = fields.RF_frequency;

    if (freqStr !== undefined) {
      const freqMhz = parseFloat(freqStr);
      updateSlice(sliceId, freqMhz);
    }

    // Also watch for TX slice designation
    if (fields.tx === '1') {
      console.log(`  ↳ Slice ${sliceId} is the TX slice`);
    }

  } else if (type === 'interlock') {
    // TX interlock state — tells us when the radio is actually transmitting
    const state = fields.state || '';
    const reason = fields.reason || '';
    if (state === 'PTT_REQUESTED' || state === 'TRANSMITTING') {
      console.log(`\n⚡ TX ACTIVE — state=${state}`);
      console.log(`  ↳ Antenna switch should lock out Radio B interlock now`);
    } else if (state === 'RECEIVE') {
      console.log(`\n RX — transmit ended`);
    }

  } else {
    // Print other status types at low verbosity
    // Uncomment below to see everything the radio sends:
     console.log(`← status: ${body}`);
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
