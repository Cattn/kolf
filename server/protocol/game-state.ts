// SPDX-License-Identifier: GPL-2.0-or-later
import type { JsonObject } from './envelope.ts';

export function integer(value: unknown, min = 0, max = 1_000_000_000): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= min && value <= max;
}

export function finite(value: unknown, min: number, max: number): value is number {
  return typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
}

function validVisual(value: JsonObject): boolean {
  if (!value || typeof value.id !== 'string' || !value.id.length || value.id.length > 512 || typeof value.visible !== 'boolean') return false;
  if (!['x', 'y', 'z', 'rotation', 'opacity'].every(key => finite(value[key], -1_000_000, 1_000_000))
    || !finite(value.opacity, 0, 1)) return false;
  if ('color' in value && (typeof value.color !== 'string' || !/^#[a-fA-F0-9]{8}$/.test(value.color))) return false;
  if (value.kind === 'line') {
    return Array.isArray(value.line) && value.line.length === 4
      && value.line.every((component: unknown) => finite(component, -1_000_000, 1_000_000));
  }
  return value.kind === 'sprite' && typeof value.sprite === 'string' && value.sprite.length > 0
    && value.sprite.length <= 128 && integer(value.frame, -1, 10_000);
}

export function validRosterState(state: JsonObject, rosterSize?: number): boolean {
  const size = rosterSize ?? (Array.isArray(state?.balls) ? state.balls.length : 0);
  const balls = state?.balls;
  const objects = state?.objects;
  const scores = state?.scores;
  return !!state && integer(size, 2, 8) && integer(state.stateRevision, 1) && integer(state.holeGeneration, 1)
    && integer(state.turnId, 1) && integer(state.activeSlot, 0, size - 1) && integer(state.hole, 1, 1000)
    && integer(state.par, 0, 1000) && ['AwaitingShot', 'Simulating', 'AwaitingHazardChoice', 'Finished'].includes(String(state.phase))
    && typeof state.manifestHash === 'string' && /^[a-f0-9]{64}$/.test(state.manifestHash)
    && typeof state.courseHash === 'string' && /^[a-f0-9]{64}$/.test(state.courseHash)
    && Array.isArray(balls) && balls.length === size
    && balls.every((ball: JsonObject, index: number) => validVisual(ball) && ball.id === `ball/${index}` && integer(ball.state, 0, 2))
    && Array.isArray(objects) && objects.length <= 4096 && objects.every((object: JsonObject) => validVisual(object))
    && new Set(objects.map((object: JsonObject) => object.id)).size === objects.length
    && Array.isArray(scores) && scores.length === size
    && scores.every((row: unknown) => Array.isArray(row) && row.length >= Number(state.hole) && row.length <= 1000
      && row.length === scores[0].length && row.every(value => integer(value, 0, 10_000)))
    && (state.phase !== 'AwaitingHazardChoice'
      || (typeof state.choiceId === 'string' && /^[A-Za-z0-9_-]{1,96}$/.test(state.choiceId) && integer(state.choiceSlot, 0, size - 1)));
}
