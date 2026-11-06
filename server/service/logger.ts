// SPDX-License-Identifier: GPL-2.0-or-later
import type { Delivery } from './connection.ts';

export type LogValue = string | number | boolean | undefined;
export type LogFields = Record<string, LogValue>;
export type Logger = (event: string, fields?: LogFields) => void;

export const silentLogger: Logger = () => {};

export function jsonLogger(write: (line: string) => void = line => process.stdout.write(`${line}\n`),
  now: () => number = () => Date.now()): Logger {
  return (event, fields = {}) => {
    const entry: Record<string, LogValue> = { time: new Date(now()).toISOString(), event };
    for (const [key, value] of Object.entries(fields)) if (value !== undefined) entry[key] = value;
    write(JSON.stringify(entry));
  };
}

const KNOWN_TYPE = /^[A-Za-z]{1,40}$/;
const SERVER_REASONS = new Set([
  'A participant left the lobby', 'A participant disconnected',
  'The lobby closed after 30 minutes without activity.', 'The match reached the 3-hour limit.',
]);

export function safeReason(reason: unknown, debug: boolean): string {
  const text = String(reason ?? '');
  return SERVER_REASONS.has(text) || debug ? text.slice(0, 160) : 'other';
}

export function requestType(raw: string): string {
  const match = /"type"\s*:\s*"([^"]{1,64})"/.exec(raw);
  return match && KNOWN_TYPE.test(match[1]) ? match[1] : 'unknown';
}

export function logDeliveries(log: Logger, deliveries: Delivery[], incomingType?: string, debug = false) {
  const seen = new Set<string>();
  for (const { message } of deliveries) {
    const payload = message.payload as Record<string, any>;
    const key = `${message.type}:${message.lobbyId ?? ''}:${message.matchId ?? ''}`;
    if (seen.has(key)) continue;
    seen.add(key);
    switch (message.type) {
      case 'LobbyCreated':
        log('lobby.created', { lobbyId: message.lobbyId });
        break;
      case 'MatchStarted':
        log('match.started', { lobbyId: message.lobbyId, matchId: message.matchId ?? payload.state?.match?.matchId,
          players: Array.isArray(payload.state?.match?.roster) ? payload.state.match.roster.length : undefined });
        break;
      case 'MatchResult': {
        const result = payload.result ?? {};
        log(result.status === 'Interrupted' ? 'match.interrupted' : 'match.finished', {
          lobbyId: message.lobbyId, matchId: result.matchId ?? message.matchId,
          reason: result.status === 'Interrupted' ? safeReason(result.reason, debug) : undefined,
          durationMs: typeof result.durationMs === 'number' ? result.durationMs : undefined,
          players: Array.isArray(result.roster) ? result.roster.length : undefined,
        });
        break;
      }
      case 'RequestRejected':
      case 'UnsupportedProtocol':
        log('request.rejected', { type: incomingType ?? 'unknown', code: String(payload.code ?? message.type) });
        break;
      case 'ServiceNotice':
        break;
    }
  }
}
