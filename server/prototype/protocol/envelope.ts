// SPDX-License-Identifier: GPL-2.0-or-later
import { validId } from './ids.ts';

export const PROTOCOL_VERSION = 2;
export const MAX_MESSAGE_BYTES = 512 * 1024;

export type JsonObject = Record<string, unknown>;

export interface Envelope<T extends string = string, P extends JsonObject = JsonObject> {
  protocolVersion: typeof PROTOCOL_VERSION;
  type: T;
  requestId?: string;
  lobbyId?: string;
  matchId?: string;
  payload: P;
}

export class ProtocolError extends Error {
  readonly code: string;
  constructor(code: string, message: string) {
    super(message);
    this.name = 'ProtocolError';
    this.code = code;
  }
}

export function isObject(value: unknown): value is JsonObject {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

export function decodeEnvelope(raw: string): Envelope {
  if (Buffer.byteLength(raw) > MAX_MESSAGE_BYTES) throw new ProtocolError('MessageTooLarge', 'message exceeds byte limit');
  let value: unknown;
  try { value = JSON.parse(raw); }
  catch { throw new ProtocolError('MalformedJson', 'message is not valid JSON'); }
  if (!isObject(value)) throw new ProtocolError('InvalidEnvelope', 'message must be an object');
  if (value.protocolVersion !== PROTOCOL_VERSION) throw new ProtocolError('UnsupportedProtocol', 'protocol version 2 is required');
  if (!validId(value.type)) throw new ProtocolError('InvalidEnvelope', 'message type is invalid');
  for (const key of ['requestId', 'lobbyId', 'matchId'] as const)
    if (value[key] !== undefined && !validId(value[key])) throw new ProtocolError('InvalidEnvelope', `${key} is invalid`);
  if (!isObject(value.payload)) throw new ProtocolError('InvalidEnvelope', 'payload must be an object');
  return value as unknown as Envelope;
}

export function envelope<T extends string, P extends JsonObject>(type: T, payload: P, scope: {
  requestId?: string; lobbyId?: string; matchId?: string;
} = {}): Envelope<T, P> {
  return { protocolVersion: PROTOCOL_VERSION, type, ...scope, payload };
}
