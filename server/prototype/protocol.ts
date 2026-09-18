// SPDX-License-Identifier: GPL-2.0-or-later
export const VERSION = 1;
export const MATCH = 'prototype';
export const MAX_BYTES = 512 * 1024;
export type Role = 'authority' | 'guest';
export type Message = Record<string, any>;
export function integer(v: unknown, min = 0, max = 1_000_000_000): v is number {
  return typeof v === 'number' && Number.isSafeInteger(v) && v >= min && v <= max;
}
export function finite(v: unknown, min: number, max: number): v is number {
  return typeof v === 'number' && Number.isFinite(v) && v >= min && v <= max;
}
export function id(v: unknown): v is string {
  return typeof v === 'string' && /^[A-Za-z0-9_.:-]{1,96}$/.test(v);
}
export function envelope(type: string, fields: Message = {}): Message {
  return { ...fields, v: VERSION, matchId: MATCH, type };
}
export function decode(raw: string): Message {
  if (Buffer.byteLength(raw) > MAX_BYTES) throw Error('oversized message');
  const m = JSON.parse(raw);
  if (!m || Array.isArray(m) || typeof m !== 'object' || m.v !== VERSION || m.matchId !== MATCH || !id(m.type))
    throw Error('invalid envelope');
  return m;
}
export function validShot(m: Message): boolean {
  // Normal putting increments by 1.5 up to the first value above 55 (55.5).
  // Advanced putting can overshoot 65 by at most 1.7, before dividing by 8.
  const max = m.puttingMode === 'normal' ? 55.5 / 8 : m.puttingMode === 'advanced' ? 66.7 / 8 : 0;
  return id(m.commandId) && integer(m.holeGeneration, 1) && integer(m.turnId, 1)
    && integer(m.playerSlot, 0, 1) && finite(m.directionRadians, -Math.PI, Math.PI)
    && finite(m.launchMagnitude, Number.MIN_VALUE, max);
}
export function shotKey(m: Message): string {
  return JSON.stringify([m.holeGeneration, m.turnId, m.playerSlot, m.puttingMode, m.directionRadians, m.launchMagnitude]);
}
export function validState(s: Message): boolean {
  return !!s && integer(s.stateRevision, 1) && integer(s.holeGeneration, 1) && integer(s.turnId, 1)
    && integer(s.activeSlot, 0, 1) && integer(s.hole, 1, 1000) && integer(s.par, 0, 1000)
    && ['AwaitingShot', 'Simulating', 'AwaitingHazardChoice', 'Finished'].includes(s.phase)
    && typeof s.manifestHash === 'string' && /^[a-f0-9]{64}$/.test(s.manifestHash)
    && typeof s.courseHash === 'string' && /^[a-f0-9]{64}$/.test(s.courseHash)
    && Array.isArray(s.balls) && s.balls.length === 2
    && s.balls.every((b: Message, i: number) => validVisual(b) && b.id === `ball/${i}` && integer(b.state, 0, 2))
    && Array.isArray(s.objects) && s.objects.length <= 4096 && s.objects.every(validVisual)
    && new Set(s.objects.map((o: Message) => o.id)).size === s.objects.length
    && Array.isArray(s.scores) && s.scores.length === 2
    && s.scores.every((row: unknown) => Array.isArray(row) && row.length === s.hole && row.every(v => integer(v, 0, 10000)))
    && (s.phase !== 'AwaitingHazardChoice' || (id(s.choiceId) && integer(s.choiceSlot, 0, 1)));
}
export function validVisual(v: Message): boolean {
  if (!v || typeof v.id !== 'string' || !v.id.length || v.id.length > 512 || typeof v.visible !== 'boolean') return false;
  if (!['x', 'y', 'z', 'rotation', 'opacity'].every(k => finite(v[k], -1000000, 1000000)) || !finite(v.opacity, 0, 1)) return false;
  if ('color' in v && (typeof v.color !== 'string' || !/^#[a-fA-F0-9]{8}$/.test(v.color))) return false;
  if (v.kind === 'line') return Array.isArray(v.line) && v.line.length === 4 && v.line.every((n: unknown) => finite(n, -1000000, 1000000));
  return v.kind === 'sprite' && typeof v.sprite === 'string' && v.sprite.length > 0 && v.sprite.length <= 128 && integer(v.frame, -1, 10000);
}
