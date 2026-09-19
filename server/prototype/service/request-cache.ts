// SPDX-License-Identifier: GPL-2.0-or-later
import { LobbyError } from './errors.ts';

type Entry = { fingerprint: string; value: unknown; expiresAt: number };

function canonical(value: unknown): string {
  if (value === undefined) return 'undefined';
  if (Array.isArray(value)) return `[${value.map(canonical).join(',')}]`;
  if (value && typeof value === 'object') return `{${Object.entries(value as Record<string, unknown>)
    .sort(([a], [b]) => a.localeCompare(b, 'en')).map(([key, item]) => `${JSON.stringify(key)}:${canonical(item)}`).join(',')}}`;
  return JSON.stringify(value);
}

export class RequestCache {
  private readonly entries = new Map<string, Entry>();
  private readonly now: () => number;
  private readonly maximum: number;
  private readonly lifetimeMs: number;
  constructor(now = () => Date.now(), maximum = 256, lifetimeMs = 10 * 60_000) {
    this.now = now; this.maximum = maximum; this.lifetimeMs = lifetimeMs;
  }

  run<T>(scope: string, requestId: string, type: string, payload: unknown, action: () => T): T {
    const key = `${scope}:${requestId}`;
    const fingerprint = canonical({ type, payload });
    this.prune();
    const known = this.entries.get(key);
    if (known) {
      if (known.fingerprint !== fingerprint) throw new LobbyError('RequestConflict', 'request ID was reused with different content');
      return structuredClone(known.value) as T;
    }
    const value = action();
    if (this.entries.size >= this.maximum) this.entries.delete(this.entries.keys().next().value!);
    this.entries.set(key, { fingerprint, value: structuredClone(value), expiresAt: this.now() + this.lifetimeMs });
    return structuredClone(value);
  }

  get size() { this.prune(); return this.entries.size; }

  private prune() {
    const now = this.now();
    for (const [key, entry] of this.entries) if (entry.expiresAt <= now) this.entries.delete(key);
  }
}
