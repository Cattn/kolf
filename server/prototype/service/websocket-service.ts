// SPDX-License-Identifier: GPL-2.0-or-later
import { WebSocket, WebSocketServer } from 'ws';
import type { AddressInfo } from 'node:net';
import { hostname } from 'node:os';
import { MAX_MESSAGE_BYTES } from '../protocol/envelope.ts';
import type { ConnectionId } from '../protocol/ids.ts';
import type { CourseCatalogEntry } from './lobby.ts';
import { LobbyError } from './errors.ts';
import { LobbyProtocolController } from './connection.ts';
import type { Delivery } from './connection.ts';
import type { ServiceLimits } from './lobby-registry.ts';

interface Options { host?: string; port?: number; catalog: CourseCatalogEntry[]; limits?: Partial<ServiceLimits> }

/** Thin WebSocket transport for the bounded v3 room registry. */
export class LobbyWebSocketService {
  readonly controller: LobbyProtocolController;
  private readonly server: WebSocketServer;
  private readonly sockets = new Map<ConnectionId, WebSocket>();
  private readonly pendingVisual = new Map<ConnectionId, string>();
  private readonly matchTimer: NodeJS.Timeout;
  private readonly frameTimer: NodeJS.Timeout;
  private stopping = false;

  constructor(options: Options) {
    this.controller = new LobbyProtocolController(options.catalog, undefined, undefined, options.limits);
    this.server = new WebSocketServer({
      host: options.host ?? '127.0.0.1', port: options.port ?? 0,
      maxPayload: MAX_MESSAGE_BYTES, perMessageDeflate: false,
    });
    this.server.on('connection', socket => this.accept(socket));
    this.matchTimer = setInterval(() => this.deliver(this.controller.tick()), 250);
    this.frameTimer = setInterval(() => this.flushVisual(), 30);
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
    clearInterval(this.matchTimer); clearInterval(this.frameTimer); this.pendingVisual.clear();
    for (const socket of this.sockets.values()) socket.close(1001, 'service shutdown');
    await new Promise<void>((resolveClose, reject) => this.server.close(error => error ? reject(error) : resolveClose()));
  }

  private accept(socket: WebSocket) {
    let connectionId: ConnectionId;
    try { connectionId = this.controller.connect(); }
    catch (error) {
      if (!(error instanceof LobbyError) || error.code !== 'ServiceFull') throw error;
      socket.send(JSON.stringify({ protocolVersion: 3, type: 'RequestRejected', payload: {
        code: error.code, message: error.message, supportedProtocolVersion: 3,
      } }));
      socket.close(1013, 'service full'); return;
    }
    this.sockets.set(connectionId, socket);
    this.deliver([this.controller.greeting(connectionId)]);
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
      this.sockets.delete(connectionId); this.pendingVisual.delete(connectionId);
      if (!this.stopping) this.deliver(this.controller.disconnect(connectionId));
    });
  }

  private deliver(deliveries: Delivery[]) {
    for (const delivery of deliveries) {
      const socket = this.sockets.get(delivery.connectionId);
      if (!socket || socket.readyState !== WebSocket.OPEN) continue;
      const text = JSON.stringify(delivery.message);
      if (Buffer.byteLength(text) > this.controller.service.limits.maximumMessageBytes
        || socket.bufferedAmount > this.controller.service.limits.maximumRetainedOutboundBytes) {
        socket.close(1009, 'outgoing queue limit');
        continue;
      }
      if (delivery.visual && socket.bufferedAmount > this.controller.service.limits.maximumMessageBytes / 2) {
        this.pendingVisual.set(delivery.connectionId, text);
        continue;
      }
      if (!delivery.visual) this.pendingVisual.delete(delivery.connectionId);
      socket.send(text);
    }
  }

  private flushVisual() {
    for (const [connectionId, text] of this.pendingVisual) {
      const socket = this.sockets.get(connectionId);
      if (!socket || socket.readyState !== WebSocket.OPEN) { this.pendingVisual.delete(connectionId); continue; }
      if (socket.bufferedAmount > this.controller.service.limits.maximumMessageBytes / 2) continue;
      this.pendingVisual.delete(connectionId); socket.send(text);
    }
  }
}
