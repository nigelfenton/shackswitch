// bridge.js — WebSocket ↔ TCP bridge
// Lets the browser dashboard talk to the Arduino R4 WiFi over raw TCP.
//
// Usage:
//   npm install ws
//   node bridge.js <arduino-ip> [tcp-port]
//
// Then open dashboard.html and connect to localhost:9009

const net = require('net');
const http = require('http');
const { WebSocketServer } = require('ws');

const ARDUINO_IP   = process.argv[2] || '192.168.1.100';
const TCP_PORT     = parseInt(process.argv[3] || '9008');
const WS_PORT      = TCP_PORT + 1;   // WebSocket on 9009

const httpServer = http.createServer();
const wss = new WebSocketServer({ server: httpServer });

console.log(`[bridge] WebSocket → TCP  ws://localhost:${WS_PORT} → ${ARDUINO_IP}:${TCP_PORT}`);

wss.on('connection', (ws) => {
  console.log('[bridge] Browser connected');

  const tcp = net.createConnection(TCP_PORT, ARDUINO_IP, () => {
    console.log(`[bridge] TCP connected to ${ARDUINO_IP}:${TCP_PORT}`);
  });

  tcp.on('data', (data) => {
    if (ws.readyState === ws.OPEN) ws.send(data.toString());
  });

  tcp.on('close', () => {
    console.log('[bridge] TCP closed');
    ws.close();
  });

  tcp.on('error', (e) => {
    console.error('[bridge] TCP error:', e.message);
    ws.close();
  });

  ws.on('message', (msg) => {
    tcp.write(msg.toString());
  });

  ws.on('close', () => {
    console.log('[bridge] Browser disconnected');
    tcp.destroy();
  });
});

httpServer.listen(WS_PORT, () => {
  console.log(`[bridge] Listening on ws://localhost:${WS_PORT}`);
});
