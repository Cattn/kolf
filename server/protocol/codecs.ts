// SPDX-License-Identifier: GPL-2.0-or-later
import { decodeEnvelope, isObject, ProtocolError } from './envelope.ts';
import type { Envelope, JsonObject } from './envelope.ts';
import { validId, validJoinCode } from './ids.ts';
import { finite, integer, validRosterState } from './game-state.ts';

export type ClientMessage =
  | Envelope<'CreateLobby', { displayName: string; colorMode: 'auto' | 'custom'; customColor?: string; courseId: string }>
  | Envelope<'JoinLobby', { joinCode: string; displayName: string; colorMode: 'auto' | 'custom'; customColor?: string }>
  | Envelope<'LeaveLobby', Record<string, never>>
  | Envelope<'UpdateMember', { displayName: string }>
  | Envelope<'AddPlayer', { displayName: string; colorMode: 'auto' | 'custom'; customColor?: string }>
  | Envelope<'UpdatePlayer', { playerId: string; displayName?: string; colorMode?: 'auto' | 'custom'; customColor?: string }>
  | Envelope<'RemovePlayer', { playerId: string }>
  | Envelope<'ReorderPlayers', { playerIds: string[] }>
  | Envelope<'SetCourse', { courseId: string }>
  | Envelope<'SetReady', { lobbyRevision: number; ready: boolean }>
  | Envelope<'StartMatch', { lobbyRevision: number }>
  | Envelope<'CourseReady', { courseHash: string; compatibilityId: string }>
  | Envelope<'PreparationFailed', { reason: string }>
  | Envelope<'ReturnToLobby', Record<string, never>>
  | Envelope<'SceneReady', { manifestHash: string }>
  | Envelope<'SubmitShot', { commandId: string; holeGeneration: number; turnId: number; playerId: string;
      puttingMode: 'normal' | 'advanced'; directionRadians: number; launchMagnitude: number }>
  | Envelope<'ShotAccepted' | 'ShotRejected', { commandId: string; reason?: string }>
  | Envelope<'InitialState' | 'CommitTransition', { state: JsonObject }>
  | Envelope<'StateApplied', { stateRevision: number; syncId: number; manifestHash: string }>
  | Envelope<'StateFrame', { state: JsonObject; syncId: number; frameSeq: number; hostMs: number }>
  | Envelope<'AimUpdate', { playerId: string; stateRevision: number; syncId: number; holeGeneration: number;
      turnId: number; directionRadians: number; strength: number }>
  | Envelope<'ChooseHazardAction', { choiceId: string; stateRevision: number; syncId: number; action: 'drop' | 'rehit' }>
  | Envelope<'RequestResync', Record<string, never>>
  | Envelope<'FullState', { state: JsonObject; syncId: number }>
  | Envelope<'MatchInterrupted', { reason?: string }>;

const hash = (value: unknown): value is string => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const revision = (value: unknown): value is number => Number.isSafeInteger(value) && (value as number) >= 1 && (value as number) <= 1_000_000_000;
const color = (value: unknown): value is string => typeof value === 'string' && /^#[0-9a-fA-F]{8}$/.test(value);
const colorChoice = (p: JsonObject) => (p.colorMode === 'auto' && p.customColor === undefined)
  || (p.colorMode === 'custom' && color(p.customColor));
const displayName = (value: unknown): value is string => typeof value === 'string' && value === value.trim()
  && [...value].length >= 1 && [...value].length <= 32 && !/[\u0000-\u001f\u007f]/u.test(value);
const reason = (value: unknown): value is string => typeof value === 'string' && value === value.trim()
  && value.length >= 1 && value.length <= 160;
const playerIds = (value: unknown): value is string[] => Array.isArray(value) && value.length >= 1 && value.length <= 8
  && value.every(validId) && new Set(value).size === value.length;

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

function requireEmptyPayload(message: Envelope) {
  if (Object.keys(message.payload).length) throw new ProtocolError('InvalidPayload', `${message.type} payload must be empty`);
}

function requireGameplayScope(message: Envelope) {
  requireMatch(message);
}

function assertState(value: unknown) {
  if (!validRosterState(value as Record<string, unknown>)) throw new ProtocolError('InvalidPayload', 'invalid authoritative state');
}

export function decodeClientMessage(raw: string): ClientMessage {
  const message = decodeEnvelope(raw);
  requireRequest(message);
  const p: JsonObject = message.payload;
  switch (message.type) {
    case 'CreateLobby':
      rejectExtraScope(message, false, false);
      if (!displayName(p.displayName) || !colorChoice(p)) throw new ProtocolError('InvalidPayload', 'invalid member profile');
      assertCourse(p.courseId);
      return message as ClientMessage;
    case 'JoinLobby':
      rejectExtraScope(message, false, false);
      if (!validJoinCode(p.joinCode) || !displayName(p.displayName) || !colorChoice(p))
        throw new ProtocolError('InvalidPayload', 'invalid join request');
      return message as ClientMessage;
    case 'LeaveLobby':
      requireLobby(message); rejectExtraScope(message, true, false); requireEmptyPayload(message);
      return message as ClientMessage;
    case 'UpdateMember':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!displayName(p.displayName)) throw new ProtocolError('InvalidPayload', 'invalid member update');
      return message as ClientMessage;
    case 'AddPlayer':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!displayName(p.displayName) || !colorChoice(p)) throw new ProtocolError('InvalidPayload', 'invalid player profile');
      return message as ClientMessage;
    case 'UpdatePlayer':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!validId(p.playerId) || (p.displayName === undefined && p.colorMode === undefined)
        || (p.displayName !== undefined && !displayName(p.displayName))
        || (p.colorMode !== undefined && !colorChoice(p)))
        throw new ProtocolError('InvalidPayload', 'invalid player update');
      return message as ClientMessage;
    case 'RemovePlayer':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!validId(p.playerId)) throw new ProtocolError('InvalidPayload', 'invalid player ID');
      return message as ClientMessage;
    case 'ReorderPlayers':
      requireLobby(message); rejectExtraScope(message, true, false);
      if (!playerIds(p.playerIds)) throw new ProtocolError('InvalidPayload', 'invalid player order');
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
    case 'SceneReady':
      requireGameplayScope(message);
      if (!hash(p.manifestHash)) throw new ProtocolError('InvalidPayload', 'manifestHash is invalid');
      return message as ClientMessage;
    case 'SubmitShot': {
      requireGameplayScope(message);
      const max = p.puttingMode === 'normal' ? 55.5 / 8 : p.puttingMode === 'advanced' ? 66.7 / 8 : 0;
      if (!validId(p.commandId) || !integer(p.holeGeneration, 1) || !integer(p.turnId, 1) || !validId(p.playerId)
        || !finite(p.directionRadians, -Math.PI, Math.PI) || !finite(p.launchMagnitude, Number.MIN_VALUE, max))
        throw new ProtocolError('InvalidPayload', 'invalid shot');
      return message as ClientMessage;
    }
    case 'ShotAccepted': case 'ShotRejected':
      requireGameplayScope(message);
      if (!validId(p.commandId) || (p.reason !== undefined && !reason(p.reason)))
        throw new ProtocolError('InvalidPayload', 'invalid shot response');
      return message as ClientMessage;
    case 'InitialState': case 'CommitTransition':
      requireGameplayScope(message); assertState(p.state); return message as ClientMessage;
    case 'StateApplied':
      requireGameplayScope(message);
      if (!integer(p.stateRevision, 1) || !integer(p.syncId, 1) || !hash(p.manifestHash))
        throw new ProtocolError('InvalidPayload', 'invalid state acknowledgement');
      return message as ClientMessage;
    case 'StateFrame':
      requireGameplayScope(message); assertState(p.state);
      if (!integer(p.syncId, 1) || !integer(p.frameSeq, 1) || !finite(p.hostMs, 0, Number.MAX_SAFE_INTEGER))
        throw new ProtocolError('InvalidPayload', 'invalid state frame');
      return message as ClientMessage;
    case 'AimUpdate':
      requireGameplayScope(message);
      if (!validId(p.playerId) || !integer(p.stateRevision, 1) || !integer(p.syncId, 1)
        || !integer(p.holeGeneration, 1) || !integer(p.turnId, 1)
        || !finite(p.directionRadians, -Math.PI, Math.PI) || !finite(p.strength, 0, 1))
        throw new ProtocolError('InvalidPayload', 'invalid aim update');
      return message as ClientMessage;
    case 'ChooseHazardAction':
      requireGameplayScope(message);
      if (!validId(p.choiceId) || !integer(p.stateRevision, 1) || !integer(p.syncId, 1)
        || (p.action !== 'drop' && p.action !== 'rehit')) throw new ProtocolError('InvalidPayload', 'invalid hazard action');
      return message as ClientMessage;
    case 'RequestResync':
      requireGameplayScope(message); requireEmptyPayload(message); return message as ClientMessage;
    case 'FullState':
      requireGameplayScope(message); assertState(p.state);
      if (!integer(p.syncId, 1)) throw new ProtocolError('InvalidPayload', 'invalid full state');
      return message as ClientMessage;
    case 'MatchInterrupted':
      requireGameplayScope(message);
      if (p.reason !== undefined && !reason(p.reason)) throw new ProtocolError('InvalidPayload', 'invalid interruption reason');
      return message as ClientMessage;
    default: throw new ProtocolError('UnknownMessage', `unsupported client message ${message.type}`);
  }
}

export const validators = { hash, revision, color, displayName, reason, isObject, identifier: validId };
