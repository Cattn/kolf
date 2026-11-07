// SPDX-License-Identifier: GPL-2.0-or-later
import { WebSocket, WebSocketServer } from 'ws';
import { createServer } from 'node:http';
import type { Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { hostname } from 'node:os';
import { MAX_MESSAGE_BYTES } from '../protocol/envelope.ts';
import type { ConnectionId, IdFactory } from '../protocol/ids.ts';
import type { CourseCatalogEntry } from './lobby.ts';
import { LobbyError } from './errors.ts';
import { LobbyProtocolController } from './connection.ts';
import type { Delivery } from './connection.ts';
import type { ServiceLimits } from './lobby-registry.ts';
import { handleHttpRequest, WEBSOCKET_PATHS } from './http-routes.ts';
import { defaultServerInfo } from './server-info.ts';
import type { ServerInfo } from './server-info.ts';
import { AddressCounter, AttemptThrottle, clientAddress, DEFAULT_ABUSE_LIMITS, parseTrustedProxies, TokenBucket } from './abuse-guard.ts';
import type { AbuseLimits } from './abuse-guard.ts';
import { logDeliveries, requestType, silentLogger } from './logger.ts';
import type { Logger } from './logger.ts';
import type { IncomingMessage } from 'node:http';
import type { BlockList } from 'node:net';

interface Options {
  host?: string; port?: number; catalog: CourseCatalogEntry[]; limits?: Partial<ServiceLimits>; info?: ServerInfo;
  abuse?: Partial<AbuseLimits>; trustedProxies?: string; now?: () => number; log?: Logger; debugLog?: boolean; ids?: IdFactory;
}

/** Thin WebSocket transport for the bounded v3 room registry. */
export class LobbyWebSocketService {
  readonly controller: LobbyProtocolController;
  private readonly http: Server;
  private readonly server: WebSocketServer;
  private readonly startedAt = Date.now();
  private readonly listening: Promise<void>;
  private readonly sockets = new Map<ConnectionId, WebSocket>();
  private readonly pendingVisual = new Map<ConnectionId, string>();
  private readonly matchTimer: NodeJS.Timeout;
  private readonly frameTimer: NodeJS.Timeout;
  private stopping = false;
  private readonly now: () => number;
  private readonly abuse: AbuseLimits;
  private readonly trustedProxies: BlockList;
  private readonly addresses: AddressCounter;
  private readonly joinFailures: AttemptThrottle;
  private readonly log: Logger;
  private readonly debugLog: boolean;

  constructor(options: Options) {
    const info = options.info ?? defaultServerInfo();
    this.now = options.now ?? (() => Date.now());
    this.log = options.log ?? silentLogger;
    this.debugLog = options.debugLog ?? false;
    this.abuse = { ...DEFAULT_ABUSE_LIMITS, ...options.abuse };
    this.trustedProxies = parseTrustedProxies(options.trustedProxies);
    this.addresses = new AddressCounter(this.abuse.maximumConnectionsPerAddress);
    this.joinFailures = new AttemptThrottle(this.abuse.joinFailuresPerWindow, this.abuse.joinFailureWindowMs, this.now);
    this.controller = new LobbyProtocolController(options.catalog, options.ids, this.now, options.limits, info);
    this.controller.log = this.log; this.controller.debugLog = this.debugLog;
    this.server = new WebSocketServer({ noServer: true, maxPayload: MAX_MESSAGE_BYTES, perMessageDeflate: false });
    this.server.on('connection', (socket: WebSocket, request: IncomingMessage) => this.accept(socket, request));
    this.http = createServer((request, response) => handleHttpRequest(request, response, {
      info, startedAt: this.startedAt, now: () => Date.now(),
      stats: () => ({ lobbies: this.controller.service.activeLobbyCount, connections: this.controller.connectionCount }),
    }));
    this.http.on('upgrade', (request, socket, head) => {
      let path = '';
      try { path = new URL(request.url ?? '/', 'http://localhost').pathname; } catch { path = ''; }
      if (this.stopping || !WEBSOCKET_PATHS.has(path)) {
        socket.end('HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n');
        return;
      }
      this.server.handleUpgrade(request, socket, head, websocket => this.server.emit('connection', websocket, request));
    });
    this.listening = new Promise((resolveReady, reject) => {
      this.http.once('listening', resolveReady);
      this.http.once('error', reject);
    });
    this.http.listen(options.port ?? 0, options.host ?? '127.0.0.1');
    this.matchTimer = setInterval(() => { this.deliver(this.observed(this.controller.tick())); this.joinFailures.prune(); }, 250);
    this.frameTimer = setInterval(() => this.flushVisual(), 30);
  }

  ready(): Promise<void> {
    return this.listening;
  }

  httpEndpoint(): string {
    return this.endpoint().replace(/^ws:/, 'http:');
  }

  endpoint(): string {
    const address = this.http.address() as AddressInfo | null;
    if (!address) throw Error('server is not listening');
    let host = address.address;
    if (host === '0.0.0.0' || host === '::') host = process.env.KOLF_ADVERTISE_HOST ?? hostname();
    const formatted = host.includes(':') ? `[${host}]` : host;
    return `ws://${formatted}:${address.port}`;
  }

  async drain(graceMs: number): Promise<void> {
    const deadline = this.now() + graceMs;
    this.deliver(this.controller.beginDrain(deadline));
    await new Promise<void>(resolveDrained => {
      const check = setInterval(() => {
        if (this.controller.service.activeMatchLobbyCount > 0 && this.now() < deadline) return;
        clearInterval(check); resolveDrained();
      }, 250);
    });
    await this.close();
  }

  async close(): Promise<void> {
    if (this.stopping) return;
    this.stopping = true;
    clearInterval(this.matchTimer); clearInterval(this.frameTimer); this.pendingVisual.clear();
    for (const socket of this.sockets.values()) socket.close(1001, 'service shutdown');
    await new Promise<void>(resolveClose => this.server.close(() => resolveClose()));
    this.http.closeAllConnections();
    await new Promise<void>((resolveClose, reject) => this.http.close(error => error ? reject(error) : resolveClose()));
  }

  private reject(socket: WebSocket, code: string, message: string, requestId?: string) {
    socket.send(JSON.stringify({ protocolVersion: 3, type: 'RequestRejected', ...(requestId ? { requestId } : {}),
      payload: { code, message, supportedProtocolVersion: 3 } }));
  }

  private accept(socket: WebSocket, request: IncomingMessage) {
    const address = clientAddress(request, this.trustedProxies);
    if (!this.addresses.acquire(address)) {
      this.log('connection.rejected', { code: 'TooManyConnections', address: this.debugLog ? address : undefined });
      this.reject(socket, 'TooManyConnections', 'too many connections from this address');
      socket.close(1013, 'too many connections'); return;
    }
    let connectionId: ConnectionId;
    try { connectionId = this.controller.connect(); }
    catch (error) {
      this.addresses.release(address);
      if (!(error instanceof LobbyError) || error.code !== 'ServiceFull') throw error;
      this.log('connection.rejected', { code: error.code });
      this.reject(socket, error.code, error.message);
      socket.close(1013, 'service full'); return;
    }
    this.sockets.set(connectionId, socket);
    this.deliver(this.controller.welcome(connectionId));
    let live = true;
    const bucket = new TokenBucket(this.abuse.messageBurst, this.abuse.messagesPerSecond, this.now);
    const heartbeat = setInterval(() => {
      if (!live) { socket.terminate(); return; }
      live = false; socket.ping();
    }, 5000);
    socket.on('pong', () => { live = true; });
    socket.on('message', (data, binary) => {
      if (binary) { socket.close(1003, 'text JSON required'); return; }
      if (!bucket.take()) {
        if (socket.readyState === WebSocket.OPEN) this.log('connection.closed', { code: 'RateLimited' });
        socket.close(1008, 'rate limit'); return;
      }
      const text = data.toString();
      const joinRequestId = this.joinRequestId(text);
      if (joinRequestId !== undefined && this.joinFailures.blocked(address)) {
        this.log('request.rejected', { type: 'JoinLobby', code: 'TooManyAttempts' });
        this.reject(socket, 'TooManyAttempts', 'too many join attempts; wait a minute and try again', joinRequestId || undefined);
        return;
      }
      let deliveries: Delivery[];
      try { deliveries = this.controller.receive(connectionId, text); }
      catch (error) {
        this.log('error', { where: 'receive', type: requestType(text), message: error instanceof Error ? error.message.slice(0, 200) : 'unknown' });
        socket.close(1011, 'internal error'); return;
      }
      logDeliveries(this.log, deliveries.filter(delivery => delivery.connectionId === connectionId
        || (delivery.message.type !== 'RequestRejected' && delivery.message.type !== 'UnsupportedProtocol')), requestType(text), this.debugLog);
      if (joinRequestId !== undefined && deliveries.some(delivery => delivery.connectionId === connectionId
        && delivery.message.type === 'RequestRejected' && delivery.message.payload.code === 'LobbyNotFound'))
        this.joinFailures.record(address);
      this.deliver(deliveries);
      if (deliveries.some(delivery => delivery.connectionId === connectionId
        && delivery.message.type === 'UnsupportedProtocol')) socket.close(1002, 'unsupported protocol');
    });
    socket.on('error', () => socket.terminate());
    socket.on('close', () => {
      clearInterval(heartbeat);
      this.addresses.release(address);
      this.sockets.delete(connectionId); this.pendingVisual.delete(connectionId);
      if (!this.stopping) this.deliver(this.observed(this.controller.disconnect(connectionId)));
    });
  }

  private observed(deliveries: Delivery[]): Delivery[] {
    logDeliveries(this.log, deliveries, undefined, this.debugLog);
    return deliveries;
  }

  private joinRequestId(text: string): string | undefined {
    if (!text.includes('JoinLobby')) return undefined;
    try {
      const parsed = JSON.parse(text);
      if (parsed?.type !== 'JoinLobby') return undefined;
      return typeof parsed.requestId === 'string' ? parsed.requestId : '';
    } catch { return undefined; }
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
