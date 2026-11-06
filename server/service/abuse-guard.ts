// SPDX-License-Identifier: GPL-2.0-or-later
import { BlockList, isIP } from 'node:net';
import type { IncomingMessage } from 'node:http';

export interface AbuseLimits {
  maximumConnectionsPerAddress: number;
  messageBurst: number;
  messagesPerSecond: number;
  joinFailuresPerWindow: number;
  joinFailureWindowMs: number;
}

export const DEFAULT_ABUSE_LIMITS: AbuseLimits = {
  maximumConnectionsPerAddress: 8,
  messageBurst: 80,
  messagesPerSecond: 40,
  joinFailuresPerWindow: 10,
  joinFailureWindowMs: 60_000,
};

function plainAddress(address: string): string {
  const trimmed = address.trim().replace(/^\[|\]$/g, '');
  return trimmed.toLowerCase().startsWith('::ffff:') && isIP(trimmed.slice(7)) === 4 ? trimmed.slice(7) : trimmed;
}

export function parseTrustedProxies(list: string | undefined): BlockList {
  const blocks = new BlockList();
  for (const entry of (list ?? '').split(',').map(item => item.trim()).filter(Boolean)) {
    const [address, prefixText] = entry.split('/');
    const plain = plainAddress(address), family = isIP(plain);
    if (!family) throw Error(`KOLF_TRUST_PROXY entry "${entry}" is not an IP address or CIDR range`);
    const type = family === 6 ? 'ipv6' : 'ipv4', maximum = family === 6 ? 128 : 32;
    const prefix = prefixText === undefined ? maximum : Number(prefixText);
    if (!Number.isInteger(prefix) || prefix < 0 || prefix > maximum)
      throw Error(`KOLF_TRUST_PROXY entry "${entry}" has an invalid prefix`);
    blocks.addSubnet(plain, prefix, type);
  }
  return blocks;
}

function trusted(blocks: BlockList, address: string): boolean {
  const family = isIP(address);
  return family !== 0 && blocks.check(address, family === 6 ? 'ipv6' : 'ipv4');
}

export function clientAddress(request: IncomingMessage, trustedProxies: BlockList): string {
  let address = plainAddress(request.socket.remoteAddress ?? 'unknown');
  if (!trusted(trustedProxies, address)) return address;
  const header = request.headers['x-forwarded-for'];
  const hops = (Array.isArray(header) ? header.join(',') : header ?? '').split(',').map(plainAddress).filter(Boolean);
  for (let index = hops.length - 1; index >= 0; --index) {
    if (!isIP(hops[index])) break;
    address = hops[index];
    if (!trusted(trustedProxies, address)) break;
  }
  return address;
}

export class TokenBucket {
  private tokens: number;
  private updatedAt: number;
  private readonly capacity: number;
  private readonly perSecond: number;
  private readonly now: () => number;

  constructor(capacity: number, perSecond: number, now: () => number) {
    this.capacity = capacity; this.perSecond = perSecond; this.now = now;
    this.tokens = capacity; this.updatedAt = now();
  }

  take(): boolean {
    const current = this.now();
    this.tokens = Math.min(this.capacity, this.tokens + (current - this.updatedAt) * this.perSecond / 1000);
    this.updatedAt = current;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }
}

export class AttemptThrottle {
  private readonly attempts = new Map<string, number[]>();
  private readonly maximum: number;
  private readonly windowMs: number;
  private readonly now: () => number;

  constructor(maximum: number, windowMs: number, now: () => number) {
    this.maximum = maximum; this.windowMs = windowMs; this.now = now;
  }

  private recent(key: string): number[] {
    const cutoff = this.now() - this.windowMs;
    const kept = (this.attempts.get(key) ?? []).filter(time => time > cutoff);
    if (kept.length) this.attempts.set(key, kept); else this.attempts.delete(key);
    return kept;
  }

  blocked(key: string): boolean { return this.recent(key).length >= this.maximum; }

  record(key: string) { this.attempts.set(key, [...this.recent(key), this.now()]); }

  prune() { for (const key of [...this.attempts.keys()]) this.recent(key); }
}

export class AddressCounter {
  private readonly counts = new Map<string, number>();
  private readonly maximum: number;

  constructor(maximum: number) { this.maximum = maximum; }

  acquire(address: string): boolean {
    const count = this.counts.get(address) ?? 0;
    if (count >= this.maximum) return false;
    this.counts.set(address, count + 1);
    return true;
  }

  release(address: string) {
    const count = (this.counts.get(address) ?? 0) - 1;
    if (count > 0) this.counts.set(address, count); else this.counts.delete(address);
  }
}
