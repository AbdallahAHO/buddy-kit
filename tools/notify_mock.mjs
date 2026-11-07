#!/usr/bin/env node
// Local stand-in for the notify API's device socket (docs/notify-protocol.md).
// Zero deps: plain `node tools/notify_mock.mjs`. Speaks the v1 contract:
// Bearer check on upgrade, "ping" → "pong", a question on connect, acks logged,
// plus the pairing endpoints (approved pairings hand out TOKEN).
//
//   PORT=8787 TOKEN=dtk_dev PAIR_AUTO=15 node tools/notify_mock.mjs
//
// PAIR_AUTO = seconds until a new pairing auto-approves (0 = only by key).
// stdin: q = question · a = approval · m = message · n = notification ·
//        r <id> = retract · s = status · k = close 4001 (revoke) · x = close 1000 ·
//        p [CODE] = approve a pairing (newest pending if no code) · e = expire pending
import { createServer } from 'node:http';
import { createHash, randomBytes, randomInt } from 'node:crypto';
import { createInterface } from 'node:readline';

const PORT = Number(process.env.PORT ?? 8787);
const TOKEN = process.env.TOKEN ?? 'dtk_dev';
const PATH = '/api/devices/connect';
const PAIR_AUTO = Number(process.env.PAIR_AUTO ?? 15);
const PAIR_TTL = Number(process.env.PAIR_TTL ?? 600);
const PAIR_INTERVAL = Number(process.env.PAIR_INTERVAL ?? 5);
const sockets = new Set();
let seq = 0;

const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);

const frame = (opcode, payload) => {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const len = body.length;
  const head = len < 126 ? Buffer.from([0x80 | opcode, len])
    : len < 65536 ? Buffer.from([0x80 | opcode, 126, len >> 8, len & 255])
    : null;
  if (!head) throw new Error('frame too large');
  return Buffer.concat([head, body]);
};

const sendText = (sock, text) => { sock.write(frame(0x1, text)); log('→', text); };
const sendAll = (obj) => sockets.forEach((s) => sendText(s, JSON.stringify(obj)));
const closeAll = (code) => sockets.forEach((s) => {
  s.write(frame(0x8, Buffer.from([code >> 8, code & 255])));
  s.end();
  log(`→ close ${code}`);
});

const asks = {
  q: () => ({ cmd: 'notify', v: 1, id: `ask_q${++seq}`, kind: 'question', title: 'Deploy reelandsheet?', body: '3 commits behind main. Ship it now or wait for the nightly build?', actions: ['Yes', 'No', 'Later'], ttl: 540 }),
  a: () => ({ cmd: 'notify', v: 1, id: `ask_a${++seq}`, kind: 'approval', title: 'Run rm -rf build/?', body: 'Agent wants to clear the build dir.', ttl: 300 }),
  m: () => ({ cmd: 'notify', v: 1, id: `ask_m${++seq}`, kind: 'message', title: 'Backup done', body: 'Nightly backup finished in 4m12s.', ttl: 3600 }),
  n: () => ({ cmd: 'notify', v: 1, id: `ask_n${++seq}`, kind: 'notification', title: 'CI green', body: 'main passed all checks.' }),
};

// Reads masked client frames; returns leftover bytes for the next chunk.
const onData = (sock, buf) => {
  while (buf.length >= 2) {
    const op = buf[0] & 0x0f;
    let len = buf[1] & 0x7f;
    let at = 2;
    if (len === 126) { if (buf.length < 4) break; len = buf.readUInt16BE(2); at = 4; }
    else if (len === 127) { if (buf.length < 10) break; len = Number(buf.readBigUInt64BE(2)); at = 10; }
    const masked = buf[1] & 0x80;
    if (buf.length < at + (masked ? 4 : 0) + len) break;
    const mask = masked ? buf.subarray(at, at + 4) : null;
    at += masked ? 4 : 0;
    const data = Buffer.from(buf.subarray(at, at + len));
    if (mask) for (let i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
    buf = buf.subarray(at + len);

    if (op === 0x8) {
      log(`← close ${data.length >= 2 ? data.readUInt16BE(0) : '(no code)'}`);
      sock.end();
    } else if (op === 0x9) sock.write(frame(0xa, data));
    else if (op === 0x1 || op === 0x2) {
      const text = data.toString();
      if (text === 'ping') { sock.write(frame(0x1, 'pong')); log('← ping → pong'); continue; }
      log('←', text);
      try {
        const m = JSON.parse(text);
        if (m.ack === 'notify' && (m.state === 'answered' || m.state === 'dismissed'))
          log(`  final: ${m.id} ${m.state}${m.answer ? ` "${m.answer}"` : ''}`);
      } catch { log('  (not JSON)'); }
    }
  }
  return buf;
};

// Pairing (device code flow): pairingId → { secret, code, status, expiresAt, host }.
const pairings = new Map();
const CODE_ALPHABET = '23456789ABCDEFGHJKMNPQRSTVWXYZ';   // Crockford minus 0 O 1 I L U
const newCode = () => Array.from({ length: 6 }, () => CODE_ALPHABET[randomInt(CODE_ALPHABET.length)]).join('');
const showCode = (c) => `${c.slice(0, 3)}-${c.slice(3)}`;

const approve = (code) => {
  const want = code?.toUpperCase().replace(/-/g, '');
  const hit = [...pairings.entries()].reverse()
    .find(([, p]) => p.status === 'pending' && (!want || p.code === want));
  if (!hit) { log('no pending pairing', code ?? ''); return false; }
  hit[1].status = 'approved';
  log(`pairing ${hit[0]} approved (${showCode(hit[1].code)})`);
  return true;
};

const json = (res, status, body) => {
  res.writeHead(status, { 'Content-Type': 'application/json' }).end(body ? JSON.stringify(body) : '');
  return status;
};

const readBody = (req) => new Promise((ok) => {
  let b = '';
  req.on('data', (c) => { b += c; });
  req.on('end', () => { try { ok(JSON.parse(b || '{}')); } catch { ok(null); } });
});

const handlePairing = async (req, res) => {
  const url = req.url.split('?')[0];
  if (req.method === 'POST' && url === '/api/devices/pair') {
    const info = await readBody(req);
    const id = `prq_${randomBytes(6).toString('hex')}`;
    const p = { secret: `prs_${randomBytes(12).toString('hex')}`, code: newCode(), status: 'pending',
      expiresAt: Date.now() + PAIR_TTL * 1000, host: req.headers.host };
    pairings.set(id, p);
    log(`pair request ${JSON.stringify(info)} → ${id} code ${showCode(p.code)}`);
    if (PAIR_AUTO > 0) setTimeout(() => p.status === 'pending' && approve(p.code), PAIR_AUTO * 1000);
    return json(res, 201, { pairingId: id, deviceSecret: p.secret, code: showCode(p.code), expiresIn: PAIR_TTL, interval: PAIR_INTERVAL });
  }
  if (req.method === 'POST' && url === '/api/devices/pair/approve') {
    const body = await readBody(req);
    return json(res, approve(body?.code) ? 201 : 404, { ok: true });
  }
  const m = url.match(/^\/api\/devices\/pair\/([\w-]+)$/);
  if (req.method === 'GET' && m) {
    const p = pairings.get(m[1]);
    if (!p || req.headers.authorization !== `Bearer ${p.secret}`) { log('  poll → 404'); return json(res, 404); }
    if (p.status === 'pending' && Date.now() > p.expiresAt) p.status = 'expired';
    if (p.status === 'consumed') return json(res, 410);
    if (p.status === 'approved') {
      p.status = 'consumed';
      log(`  poll ${m[1]} → approved, token handed out`);
      return json(res, 200, { status: 'approved', token: TOKEN, destinationId: 'dst_mock',
        connect: `ws://${p.host}${PATH}` });
    }
    return json(res, 200, { status: p.status });
  }
  return json(res, 404);
};

const server = createServer((req, res) => { handlePairing(req, res).catch((e) => { log('error', e.message); json(res, 500); }); });

server.on('upgrade', (req, sock) => {
  const auth = req.headers.authorization ?? '';
  log(`upgrade ${req.url} from ${sock.remoteAddress} auth=${auth ? `${auth.slice(0, 14)}…` : '(none)'}`);
  const reject = (code, msg) => { sock.end(`HTTP/1.1 ${code} ${msg}\r\nContent-Length: 0\r\n\r\n`); log(`  ${code}`); };
  if (req.url !== PATH) return reject(404, 'Not Found');
  if (!/websocket/i.test(req.headers.upgrade ?? '')) return reject(426, 'Upgrade Required');
  if (auth !== `Bearer ${TOKEN}`) return reject(401, 'Unauthorized');

  const accept = createHash('sha1')
    .update(`${req.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest('base64');
  sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  sock.setNoDelay(true);
  sockets.add(sock);
  log(`  101 (${sockets.size} connected)`);

  let pending = Buffer.alloc(0);
  sock.on('data', (chunk) => { pending = onData(sock, Buffer.concat([pending, chunk])); });
  sock.on('close', () => { sockets.delete(sock); log(`socket closed (${sockets.size} connected)`); });
  sock.on('error', (e) => log('socket error', e.message));

  sendText(sock, JSON.stringify(asks.q()));
});

createInterface({ input: process.stdin }).on('line', (line) => {
  const [cmd, arg] = line.trim().split(/\s+/);
  if (asks[cmd]) sendAll(asks[cmd]());
  else if (cmd === 'r' && arg) sendAll({ cmd: 'notify', v: 1, id: arg, retract: true, reason: 'cancelled' });
  else if (cmd === 's') sendAll({ cmd: 'status' });
  else if (cmd === 'k') closeAll(4001);
  else if (cmd === 'x') closeAll(1000);
  else if (cmd === 'p') approve(arg);
  else if (cmd === 'e') pairings.forEach((p) => { if (p.status === 'pending') p.status = 'expired'; });
  else if (cmd) log('keys: q a m n | r <id> | s | k (4001) | x (1000) | p [CODE] | e');
});

server.listen(PORT, '0.0.0.0', () =>
  log(`notify mock on ws://0.0.0.0:${PORT}${PATH}  token=${TOKEN}  pairing auto-approve=${PAIR_AUTO || 'off'}s`));
