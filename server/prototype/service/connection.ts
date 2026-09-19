// SPDX-License-Identifier: GPL-2.0-or-later
import { decodeClientMessage } from '../protocol/codecs.ts';
import { envelope, ProtocolError } from '../protocol/envelope.ts';
import type { Envelope, JsonObject } from '../protocol/envelope.ts';
import type { ConnectionId, IdFactory, MatchId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import { LobbyError } from './errors.ts';
import type { CourseCatalogEntry, LobbyState } from './lobby.ts';
import { LobbyService } from './lobby-service.ts';
import { MatchSession } from './match-session.ts';
import type { MatchProgress } from './match-session.ts';

export interface Delivery { connectionId: ConnectionId; message: Envelope }

function publicState(state: LobbyState): JsonObject {
  return {
    ...state,
    members: state.members.map(({ connectionId: _connectionId, ...member }) => member),
  } as unknown as JsonObject;
}

/** Runtime-validated protocol routing with no WebSocket dependency. */
export class LobbyProtocolController {
  readonly service: LobbyService;
  private readonly connections = new Set<ConnectionId>();
  private readonly matches = new Map<MatchId, MatchSession>();
  private readonly ids: IdFactory;

  constructor(catalog: CourseCatalogEntry[], ids: IdFactory = cryptoIds, now = () => Date.now()) {
    this.ids = ids;
    this.service = new LobbyService(catalog, ids, now);
  }

  connect(): ConnectionId {
    const connectionId = this.ids.connection();
    this.connections.add(connectionId);
    return connectionId;
  }

  disconnect(connectionId: ConnectionId): Delivery[] {
    if (!this.connections.delete(connectionId)) return [];
    try {
      const { lobby } = this.service.session(connectionId);
      const state = lobby.state();
      const recipients = state.members.filter(member => member.connectionId !== connectionId
        && this.connections.has(member.connectionId)).map(member => member.connectionId);
      if (state.match?.matchId) this.matches.delete(state.match.matchId);
      this.service.disconnect(connectionId);
      return recipients.map(recipient => ({
        connectionId: recipient,
        message: envelope('LobbyClosed', { reason: 'A participant disconnected' }, {
          lobbyId: state.lobbyId, matchId: state.match?.matchId,
        }),
      }));
    } catch (error) {
      if (error instanceof LobbyError && error.code === 'NotMember') return [];
      throw error;
    }
  }

  receive(connectionId: ConnectionId, raw: string): Delivery[] {
    if (!this.connections.has(connectionId)) return [this.error(connectionId, undefined, 'UnknownConnection', 'connection is not active')];
    let requestId: string | undefined;
    try {
      const message = decodeClientMessage(raw);
      requestId = message.requestId;
      switch (message.type) {
        case 'CreateLobby': {
          const created = this.service.create(connectionId, message.requestId!, {
            displayName: message.payload.displayName, color: message.payload.color,
          }, message.payload.courseId);
          return [{ connectionId, message: envelope('LobbyCreated', {
            memberId: created.memberId, state: publicState(created.state),
          }, { requestId, lobbyId: created.state.lobbyId }) }];
        }
        case 'JoinLobby': {
          const joined = this.service.join(connectionId, message.requestId!, message.payload.joinCode, {
            displayName: message.payload.displayName, color: message.payload.color,
          });
          return this.broadcast(joined.state, 'LobbyState', { joinedMemberId: joined.memberId }, requestId);
        }
        default: {
          const { lobby, memberId } = this.service.session(connectionId);
          if (message.lobbyId !== lobby.lobbyId) throw new LobbyError('WrongLobby', 'message lobby identity does not match the connection');
          switch (message.type) {
            case 'UpdateMember': lobby.updateMember(memberId, message.requestId!, message.payload); break;
            case 'SetCourse': lobby.setCourse(memberId, message.requestId!, message.payload.courseId); break;
            case 'SetReady': lobby.setReady(memberId, message.requestId!, message.payload.lobbyRevision, message.payload.ready); break;
            case 'StartMatch': lobby.start(memberId, message.requestId!, message.payload.lobbyRevision); break;
            case 'CourseReady': {
              const result = lobby.courseReady(memberId, message.requestId!, message.matchId!, message.payload.courseHash,
                message.payload.compatibilityId);
              if (result.allReady) {
                const match = new MatchSession(lobby.state());
                this.matches.set(message.matchId!, match);
                return [...this.broadcast(lobby.state(), 'PreparationReady', {}, requestId),
                  ...this.applyMatchProgress(lobby, match, match.start())];
              }
              break;
            }
            case 'PreparationFailed':
              lobby.abortPreparation(message.matchId!, message.payload.reason);
              this.matches.delete(message.matchId!);
              return this.broadcast(lobby.state(), 'PreparationAborted', { reason: message.payload.reason }, requestId);
            case 'ReturnToLobby': lobby.returnToLobby(memberId, message.requestId!, message.matchId!); break;
            default: {
              const match = this.matches.get(message.matchId! as MatchId);
              if (!match || message.matchId !== lobby.currentMatchId)
                throw new LobbyError('StaleMatch', 'message does not belong to the active match');
              return this.applyMatchProgress(lobby, match, match.receive(memberId, message));
            }
          }
          return this.broadcast(lobby.state(), 'LobbyState', {}, requestId);
        }
      }
    } catch (error) {
      if (error instanceof ProtocolError) return [this.error(connectionId, requestId, error.code, error.message)];
      if (error instanceof LobbyError) return [this.error(connectionId, requestId, error.code, error.message)];
      throw error;
    }
  }

  tick(): Delivery[] {
    const deliveries: Delivery[] = [];
    for (const match of [...this.matches.values()]) {
      let lobby;
      try {
        lobby = this.service.activeLobby(match.lobbyIdentity);
      } catch (error) {
        if (error instanceof LobbyError && error.code === 'LobbyNotFound') {
          this.matches.delete(match.matchId);
          continue;
        }
        throw error;
      }
      deliveries.push(...this.applyMatchProgress(lobby, match, match.tick()));
    }
    return deliveries;
  }

  beginPlaying(lobbyId: string, matchId: MatchId): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    return this.broadcast(lobby.completePreparation(matchId), 'MatchStarted');
  }

  finish(lobbyId: string, matchId: MatchId, scores: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    const result = lobby.completeMatch(matchId, scores);
    this.matches.delete(matchId);
    return this.broadcast(lobby.state(), 'MatchResult', { result });
  }

  interrupt(lobbyId: string, matchId: MatchId, reason: string, scores?: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    const result = lobby.interruptMatch(matchId, reason, scores);
    this.matches.delete(matchId);
    return this.broadcast(lobby.state(), 'MatchResult', { result });
  }

  private applyMatchProgress(lobby: ReturnType<LobbyService['activeLobby']>, match: MatchSession, progress: MatchProgress): Delivery[] {
    const gameplay = progress.deliveries.map(delivery => ({
      connectionId: lobby.member(delivery.memberId).connectionId,
      message: delivery.message,
    }));
    const deliveries: Delivery[] = [];
    if (progress.becamePlaying)
      deliveries.push(...this.broadcast(lobby.completePreparation(match.matchId), 'MatchStarted'));
    deliveries.push(...gameplay);
    if (progress.completedScores) {
      const result = lobby.completeMatch(match.matchId, progress.completedScores);
      this.matches.delete(match.matchId);
      deliveries.push(...this.broadcast(lobby.state(), 'MatchResult', { result }));
    } else if (progress.interruptedReason) {
      const result = lobby.interruptMatch(match.matchId, progress.interruptedReason);
      this.matches.delete(match.matchId);
      deliveries.push(...this.broadcast(lobby.state(), 'MatchResult', { result }));
    }
    return deliveries;
  }

  private broadcast(state: LobbyState, type: string, extra: JsonObject = {}, requestId?: string): Delivery[] {
    const payload = { state: publicState(state), ...extra };
    return state.members.filter(member => this.connections.has(member.connectionId)).map(member => ({
      connectionId: member.connectionId,
      message: envelope(type, payload, { requestId, lobbyId: state.lobbyId, matchId: state.match?.matchId }),
    }));
  }

  private error(connectionId: ConnectionId, requestId: string | undefined, code: string, message: string): Delivery {
    return { connectionId, message: envelope(code === 'UnsupportedProtocol' ? 'UnsupportedProtocol' : 'RequestRejected',
      { code, message, supportedProtocolVersion: 2 }, { requestId }) };
  }
}
