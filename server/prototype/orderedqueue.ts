// SPDX-License-Identifier: GPL-2.0-or-later
/** Application-message delay, not TCP packet loss. Bounded and order preserving.
 * Only consecutive unsent visual frames are coalesced, never across controls.
 */
export class OrderedQueue {
  private items: { text: string; visual: boolean; due: number }[] = [];
  bytes = 0;
  coalesced = 0;
  readonly limit: number;
  constructor(limit = 1024 * 1024) { this.limit = limit; }
  push(text: string, visual: boolean, due: number): boolean {
    const last = this.items.at(-1);
    if (visual && last?.visual) {
      this.bytes -= Buffer.byteLength(last.text); this.items.pop(); this.coalesced++;
      due = Math.min(due, last.due);
    }
    due = Math.max(due, this.items.at(-1)?.due ?? due);
    const bytes = Buffer.byteLength(text);
    if (this.bytes + bytes > this.limit || this.items.length >= 128) return false;
    this.items.push({ text, visual, due }); this.bytes += bytes; return true;
  }
  drain(now: number, send: (text: string) => void, canSend = () => true) {
    while (this.items.length && this.items[0].due <= now && canSend()) {
      const item = this.items.shift()!; this.bytes -= Buffer.byteLength(item.text); send(item.text);
    }
  }
}
