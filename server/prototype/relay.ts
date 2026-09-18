// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { performance } from 'node:perf_hooks';
import { WebSocket, WebSocketServer } from 'ws';
import { decode, envelope, MAX_BYTES } from './protocol.ts';
import type { Message, Role } from './protocol.ts';
import { Session } from './session.ts';
import type { Peer } from './session.ts';
import { OrderedQueue } from './orderedqueue.ts';

const host = process.env.KOLF_BIND ?? '127.0.0.1';
const port = Number(process.env.KOLF_PORT ?? 3011);
const directory = resolve(process.env.KOLF_SESSION_DIR ?? 'local-session');
const course = process.env.KOLF_COURSE;
const delayMs = Number(process.env.KOLF_DELAY_MS ?? 0);
if (!Number.isFinite(delayMs) || delayMs < 0 || delayMs > 5000) throw Error('Invalid application delay');
if (!course) throw Error('Set KOLF_COURSE to the preinstalled course path');
mkdirSync(directory, { recursive: true });
const credentials = { authority: randomBytes(24).toString('hex'), guest: randomBytes(24).toString('hex') };
for (const role of ['authority', 'guest'] as const) {
  writeFileSync(resolve(directory, `${role}.json`), JSON.stringify({ role, credential: credentials[role],
    endpoint: `ws://${host}:${port}`, course: resolve(course), logDirectory: resolve(directory, role) }, null, 2), { mode: 0o600 });
}
const session = new Session(credentials);
const server = new WebSocketServer({ host, port, maxPayload: MAX_BYTES, perMessageDeflate: false });
const log = (event: string, detail: Message = {}) => console.log(JSON.stringify({ ms: Math.round(performance.now()), event, ...detail }));
server.on('listening', () => log('listening', { host, port, configDirectory: directory }));
server.on('error', error => { log('server-error', { reason: error.message }); process.exitCode = 1; });
server.on('connection', socket => {
  let peer: Peer | undefined;
  let live = true;
  let blockedSince = 0;
  let windowStart = Date.now(), count = 0;
  const inbound = new OrderedQueue(), outbound = new OrderedQueue();
  const send = (message: Message, visual = false) => {
    const text = JSON.stringify(message);
    if (socket.readyState !== WebSocket.OPEN) return;
    if (text.length > MAX_BYTES || socket.bufferedAmount > 2 * MAX_BYTES) {
      session.interrupt('outgoing queue limit'); socket.terminate(); return;
    }
    if (!outbound.push(text, visual, Date.now() + delayMs)) { session.interrupt('bounded delayed queue exceeded'); socket.terminate(); }
  };
  const helloTimeout = setTimeout(() => { if (!peer) socket.close(1008, 'hello timeout'); }, 5000);
  const flush = setInterval(() => {
    inbound.drain(Date.now(), raw => {
      try { if (peer) session.receive(peer, raw); }
      catch { session.interrupt('protocol error'); socket.close(1008, 'protocol error'); }
    });
    if (socket.readyState === WebSocket.OPEN)
      outbound.drain(Date.now(), text => socket.send(text), () => socket.bufferedAmount < MAX_BYTES / 2);
    if (outbound.bytes && socket.bufferedAmount >= MAX_BYTES / 2) blockedSince ||= Date.now();
    else blockedSince = 0;
    if (blockedSince && Date.now() - blockedSince > 5000) { session.interrupt('slow receiver'); socket.terminate(); }
  }, 30);
  const heartbeat = setInterval(() => {
    if (!live) { socket.terminate(); return; }
    live = false; socket.ping();
  }, 5000);
  socket.on('pong', () => { live = true; });
  socket.on('message', (data, binary) => {
    try {
      if (binary) throw Error('text JSON required');
      if (Date.now() - windowStart >= 1000) { windowStart = Date.now(); count = 0; }
      if (++count > (peer?.role === 'authority' ? 150 : 40)) throw Error('rate limit');
      const raw = data.toString();
      if (!peer) {
        const hello = decode(raw);
        if (!['authority', 'guest'].includes(hello.role)) throw Error('invalid role');
        const candidate: Peer = { role: hello.role as Role, send, close: () => socket.close() };
        session.join(candidate, hello); peer = candidate; clearTimeout(helloTimeout);
        log('joined', { role: peer.role });
      } else {
        if (!inbound.push(raw, false, Date.now() + delayMs)) throw Error('bounded delayed queue exceeded');
      }
    } catch (error) {
      // Credentials/payloads deliberately never enter diagnostics.
      log('protocol-error', { role: peer?.role ?? 'unassigned', reason: (error as Error).message });
      if (peer) session.interrupt('protocol error');
      socket.close(1008, 'protocol error');
    }
  });
  socket.on('error', () => socket.terminate());
  socket.on('close', () => {
    clearTimeout(helloTimeout); clearInterval(flush); clearInterval(heartbeat);
    if (peer) session.disconnect(peer);
    log('closed', { role: peer?.role ?? 'unassigned', coalesced: outbound.coalesced });
  });
});
const watchdog = setInterval(() => session.tick(), 1000);
process.on('SIGINT', () => {
  clearInterval(watchdog); session.interrupt('relay shutdown');
  for (const socket of server.clients) socket.close(1001, 'relay shutdown');
  server.close();
});
