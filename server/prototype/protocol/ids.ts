// SPDX-License-Identifier: GPL-2.0-or-later
import { randomBytes } from 'node:crypto';

export type ConnectionId = string;
export type MemberId = string;
export type LobbyId = string;
export type JoinCode = string;
export type PlayerId = string;
export type MatchId = string;
export type RequestId = string;

export interface IdFactory {
  connection(): ConnectionId;
  member(): MemberId;
  lobby(): LobbyId;
  player(): PlayerId;
  match(): MatchId;
  joinCode(): JoinCode;
}

const token = (prefix: string) => `${prefix}_${randomBytes(12).toString('base64url')}`;
const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

export const cryptoIds: IdFactory = {
  connection: () => token('connection'),
  member: () => token('member'),
  lobby: () => token('lobby'),
  player: () => token('player'),
  match: () => token('match'),
  joinCode: () => Array.from(randomBytes(8), byte => alphabet[byte % alphabet.length]).join(''),
};

export function validId(value: unknown): value is string {
  return typeof value === 'string' && /^[A-Za-z0-9_-]{1,96}$/.test(value);
}

export function validJoinCode(value: unknown): value is JoinCode {
  return typeof value === 'string' && /^[A-HJ-NP-Z2-9]{6,12}$/.test(value);
}
