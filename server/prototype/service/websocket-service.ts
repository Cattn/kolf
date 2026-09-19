// SPDX-License-Identifier: GPL-2.0-or-later
import { WebSocket, WebSocketServer } from 'ws';
import type { AddressInfo } from 'node:net';
import { hostname } from 'node:os';
import { MAX_MESSAGE_BYTES } from '../protocol/envelope.ts';
import type { ConnectionId } from '../protocol/ids.ts';
import type { CourseCatalogEntry } from './lobby.ts';
import { LobbyProtocolController } from './connection.ts';
import type { Delivery } from './connection.ts';

interface Options { host?: string; port?: number; catalog: CourseCatalogEntry[] }

/** Thin WebSocket transport for the v2 lobby domain. */
export class LobbyWebSocketService {
  readonly controller: LobbyProtocolController;
  private readonly server: WebSocketServer;
  private readonly sockets = new Map<ConnectionId, WebSocket>();
  private readonly matchTimer: NodeJS.Timeout;
  private stopping = false;

  constructor(options: Options) {
    this.controller = new LobbyProtocolController(options.catalog);
    this.server = new WebSocketServer({
      host: options.host ?? '127.0.0.1', port: options.port ?? 0,
      maxPayload: MAX_MESSAGE_BYTES, perMessageDeflate: false,
    });
    this.server.on('connection', socket => this.accept(socket));
    this.matchTimer = setInterval(() => this.deliver(this.controller.tick()), 250);
  }

  ready(): Promise<void> {
    if (this.server.address()) return Promise.resolve();
    return new Promise((resolveReady, reject) => {
      this.server.once('listening', resolveReady);
      this.server.once('error', reject);
    });
  }

  endpoint(): string {
    const address = this.server.address() as AddressInfo | null;
    if (!address) throw Error('server is not listening');
    let host = address.address;
    if (host === '0.0.0.0' || host === '::') host = process.env.KOLF_ADVERTISE_HOST ?? hostname();
    const formatted = host.includes(':') ? `[${host}]` : host;
    return `ws://${formatted}:${address.port}`;
  }

  async close(): Promise<void> {
    this.stopping = true;
    clearInterval(this.matchTimer);
    for (const socket of this.sockets.values()) socket.close(1001, 'service shutdown');
    await new Promise<void>((resolveClose, reject) => this.server.close(error => error ? reject(error) : resolveClose()));
  }

  private accept(socket: WebSocket) {
    const connectionId = this.controller.connect();
    this.sockets.set(connectionId, socket);
    let live = true, windowStart = Date.now(), count = 0;
    const heartbeat = setInterval(() => {
      if (!live) { socket.terminate(); return; }
      live = false; socket.ping();
    }, 5000);
    socket.on('pong', () => { live = true; });
    socket.on('message', (data, binary) => {
      if (binary) { socket.close(1003, 'text JSON required'); return; }
      if (Date.now() - windowStart >= 1000) { windowStart = Date.now(); count = 0; }
      if (++count > 40) { socket.close(1008, 'rate limit'); return; }
      const deliveries = this.controller.receive(connectionId, data.toString());
      this.deliver(deliveries);
      if (deliveries.some(delivery => delivery.connectionId === connectionId
        && delivery.message.type === 'UnsupportedProtocol')) socket.close(1002, 'unsupported protocol');
    });
    socket.on('error', () => socket.terminate());
    socket.on('close', () => {
      clearInterval(heartbeat);
      this.sockets.delete(connectionId);
      if (!this.stopping) this.deliver(this.controller.disconnect(connectionId));
    });
  }

  private deliver(deliveries: Delivery[]) {
    for (const delivery of deliveries) {
      const socket = this.sockets.get(delivery.connectionId);
      if (!socket || socket.readyState !== WebSocket.OPEN) continue;
      const text = JSON.stringify(delivery.message);
      if (Buffer.byteLength(text) > MAX_MESSAGE_BYTES || socket.bufferedAmount > 2 * MAX_MESSAGE_BYTES) {
        socket.close(1009, 'outgoing queue limit');
        continue;
      }
      socket.send(text);
    }
  }
}
