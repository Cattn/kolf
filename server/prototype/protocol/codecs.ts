// SPDX-License-Identifier: GPL-2.0-or-later
import { decodeEnvelope, isObject, ProtocolError } from './envelope.ts';
import type { Envelope, JsonObject } from './envelope.ts';
import { validId, validJoinCode } from './ids.ts';

export type ClientMessage =
  | Envelope<'CreateLobby', { displayName: string; color: string; courseId: string }>
  | Envelope<'JoinLobby', { joinCode: string; displayName: string; color: string }>
  | Envelope<'UpdateMember', { displayName?: string; color?: string }>
  | Envelope<'SetCourse', { courseId: string }>
  | Envelope<'SetReady', { lobbyRevision: number; ready: boolean }>
  | Envelope<'StartMatch', { lobbyRevision: number }>
  | Envelope<'CourseReady', { courseHash: string; compatibilityId: string }>
  | Envelope<'PreparationFailed', { reason: string }>
  | Envelope<'ReturnToLobby', Record<string, never>>;

const hash = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const revision = (value: unknown): value is number => Number.isSafeInteger(value) && (value as number) >= 1 && (value as number) <= 1_000_000_000;
const color = (value: unknown): value is string => typeof value === 'string' && /^#[0-9a-fA-F]{8}$/.test(value);
const displayName = (value: unknown): value is string => typeof value === 'string' && value === value.trim()
  && [...value].length >= 1 && [...value].length <= 32 && !/[\u0000-\u001f\u007f]/u.test(value);
const reason = (value: unknown): value is string => typeof value === 'string' && value === value.trim()
  && value.length >= 1 && value.length <= 160;

function requireRequest(message: Envelope) {
  if (!message.requestId) throw new ProtocolError('InvalidEnvelope', `${message.type} requires requestId`);
}
function requireLobby(message: Envelope) {
  if (!message.lobbyId) throw new ProtocolError('InvalidEnvelope', `${message.type} requires lobbyId`);
}
function requireMatch(message: Envelope) {
  requireLobby(message);
  if (!message.matchId) throw new ProtocolError('InvalidEnvelope', `${message.type} requires matchId`);
}
function rejectExtraScope(message: Envelope, lobby: boolean, match: boolean) {
  if (!lobby && message.lobbyId) throw new ProtocolError('InvalidEnvelope', `${message.type} cannot carry lobbyId`);
  if (!match && message.matchId) throw new ProtocolError('InvalidEnvelope', `${message.type} cannot carry matchId`);
}
function assertCourse(value: unknown): asserts value is string {
  if (!validId(value)) throw new ProtocolError('InvalidPayload', 'courseId is invalid');
}

export function decodeClientMessage(raw: string): ClientMessage {
  const message = decodeEnvelope(raw);
  requireRequest(message);
  const p: JsonObject = message.payload;
  switch (message.type) {
    case 'CreateLobby':
      rejectExtraScope(message, false, false);
      if (!displayName(p.displayName) || !color(p.color)) throw new ProtocolError('InvalidPayload', 'invalid member profile');
      assertCourse(p.courseId);
      return message as ClientMessage;
    case 'JoinLobby':
      rejectExtraScope(message, false, false);
      if (!validJoinCode(p.joinCode) || !displayName(p.displayName) || !color(p.color))
        throw new ProtocolError('InvalidPayload', 'invalid join request');
      return message as ClientMessage;
    case 'UpdateMember':
      requireLobby(message); rejectExtraScope(message, true, false);
      if ((!displayName(p.displayName) && p.displayName !== undefined) || (!color(p.color) && p.color !== undefined)
        || (p.displayName === undefined && p.color === undefined)) throw new ProtocolError('InvalidPayload', 'invalid member update');
      return message as ClientMessage;
    case 'SetCourse':
      requireLobby(message); rejectExtraScope(message, true, false); assertCourse(p.courseId);
      return message as ClientMessage;
    case 'SetReady':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!revision(p.lobbyRevision) || typeof p.ready !== 'boolean') throw new ProtocolError('InvalidPayload', 'invalid readiness');
      return message as ClientMessage;
    case 'StartMatch':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!revision(p.lobbyRevision)) throw new ProtocolError('InvalidPayload', 'invalid lobby revision');
      return message as ClientMessage;
    case 'CourseReady':
      requireMatch(message);
      if (!hash(p.courseHash) || !hash(p.compatibilityId)) throw new ProtocolError('InvalidPayload', 'invalid course readiness');
      return message as ClientMessage;
    case 'PreparationFailed':
      requireMatch(message);
      if (!reason(p.reason)) throw new ProtocolError('InvalidPayload', 'invalid preparation failure');
      return message as ClientMessage;
    case 'ReturnToLobby':
      requireMatch(message);
      if (Object.keys(p).length) throw new ProtocolError('InvalidPayload', 'return payload must be empty');
      return message as ClientMessage;
    default: throw new ProtocolError('UnknownMessage', `unsupported client message ${message.type}`);
  }
}

export const validators = { hash, revision, color, displayName, reason, isObject };
