// SPDX-License-Identifier: GPL-2.0-or-later
import { decodeClientMessage } from '../protocol/codecs.ts';
import { envelope, PROTOCOL_VERSION, ProtocolError } from '../protocol/envelope.ts';
import type { Envelope, JsonObject } from '../protocol/envelope.ts';
import type { ConnectionId, IdFactory, MatchId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import { LobbyError } from './errors.ts';
import type { CourseCatalogEntry, LobbyState } from './lobby.ts';
import { LobbyRegistry } from './lobby-registry.ts';
import type { Departure, ServiceLimits } from './lobby-registry.ts';
import { MatchCoordinator } from './match-session.ts';
import type { MatchProgress } from './match-session.ts';

export interface Delivery { connectionId: ConnectionId; message: Envelope; visual?: boolean }

function publicState(state: LobbyState): JsonObject {
  return {
    ...state,
    members: state.members.map(({ connectionId: _connectionId, ...member }) => member),
  } as unknown as JsonObject;
}

/** Runtime-validated v3 routing with no WebSocket dependency. */
export class LobbyProtocolController {
  readonly service: LobbyRegistry;
  private readonly connections = new Set<ConnectionId>();
  private readonly matches = new Map<MatchId, MatchCoordinator>();
  private readonly ids: IdFactory;

  constructor(catalog: CourseCatalogEntry[], ids: IdFactory = cryptoIds, now = () => Date.now(),
    limits: Partial<ServiceLimits> = {}) {
    this.ids = ids; this.service = new LobbyRegistry(catalog, ids, now, limits);
  }

  connect(): ConnectionId {
    if (this.connections.size >= this.service.limits.maximumConnections)
      throw new LobbyError('ServiceFull', 'the service has reached its connection limit');
    const connectionId = this.ids.connection(); this.connections.add(connectionId); return connectionId;
  }

  greeting(connectionId: ConnectionId): Delivery {
    if (!this.connections.has(connectionId)) throw new LobbyError('UnknownConnection', 'connection is not active');
    return { connectionId, message: envelope('ServiceHello', {
      protocolVersion: PROTOCOL_VERSION,
      courses: this.service.catalog.map(({ expectedHash: _expectedHash, par: _par, ...course }) => course),
      limits: {
        maximumLobbies: this.service.limits.maximumLobbies,
        maximumMembersPerLobby: this.service.limits.maximumMembersPerLobby,
        maximumPlayersPerLobby: this.service.limits.maximumPlayersPerLobby,
        maximumMessageBytes: this.service.limits.maximumMessageBytes,
      },
    }) };
  }

  disconnect(connectionId: ConnectionId): Delivery[] {
    if (!this.connections.delete(connectionId)) return [];
    return this.departureDeliveries(this.service.disconnect(connectionId));
  }

  receive(connectionId: ConnectionId, raw: string): Delivery[] {
    if (!this.connections.has(connectionId)) return [this.error(connectionId, undefined, 'UnknownConnection', 'connection is not active')];
    let requestId: string | undefined;
    try {
      const message = decodeClientMessage(raw); requestId = message.requestId;
      switch (message.type) {
        case 'CreateLobby': {
          const profile = { displayName: message.payload.displayName };
          const player = { displayName: message.payload.displayName, color: message.payload.color };
          const created = this.service.create(connectionId, message.requestId!, profile, player, message.payload.courseId);
          return [{ connectionId, message: envelope('LobbyCreated', {
            memberId: created.memberId, state: publicState(created.state),
          }, { requestId, lobbyId: created.state.lobbyId }) }];
        }
        case 'JoinLobby': {
          const profile = { displayName: message.payload.displayName };
          const player = { displayName: message.payload.displayName, color: message.payload.color };
          const joined = this.service.join(connectionId, message.requestId!, message.payload.joinCode, profile, player);
          return this.broadcast(joined.state, 'LobbyState', { joinedMemberId: joined.memberId }, requestId);
        }
        default: {
          const { lobby, memberId } = this.service.session(connectionId);
          if (message.lobbyId !== lobby.lobbyId) throw new LobbyError('WrongLobby', 'message lobby identity does not match the connection');
          switch (message.type) {
            case 'LeaveLobby': return this.departureDeliveries(this.service.leave(connectionId), requestId);
            case 'UpdateMember': lobby.updateMember(memberId, message.requestId!, message.payload); break;
            case 'AddPlayer': lobby.addPlayer(memberId, message.requestId!, message.payload); break;
            case 'UpdatePlayer': lobby.updatePlayer(memberId, message.requestId!, message.payload.playerId, message.payload); break;
            case 'RemovePlayer': lobby.removePlayer(memberId, message.requestId!, message.payload.playerId); break;
            case 'ReorderPlayers': lobby.reorderPlayers(memberId, message.requestId!, message.payload.playerIds); break;
            case 'SetCourse': lobby.setCourse(memberId, message.requestId!, message.payload.courseId); break;
            case 'SetReady': lobby.setReady(memberId, message.requestId!, message.payload.lobbyRevision, message.payload.ready); break;
            case 'StartMatch': lobby.start(memberId, message.requestId!, message.payload.lobbyRevision); break;
            case 'CourseReady': {
              let result;
              try {
                result = lobby.courseReady(memberId, message.requestId!, message.matchId!, message.payload.courseHash,
                  message.payload.compatibilityId);
              } catch (error) {
                if (error instanceof LobbyError && error.code === 'StaleMatch' && lobby.phase === 'Open')
                  return this.broadcast(lobby.state(), 'LobbyState', {}, requestId);
                if (!(error instanceof LobbyError)
                  || (error.code !== 'CourseMismatch' && error.code !== 'CompatibilityMismatch')) throw error;
                const state = lobby.abortPreparation(message.matchId!, error.message);
                this.matches.delete(message.matchId!);
                return this.broadcast(state, 'PreparationAborted', { reason: error.message }, requestId);
              }
              if (result.allReady) {
                const match = new MatchCoordinator(lobby.state()); this.matches.set(message.matchId!, match);
                return [...this.broadcast(lobby.state(), 'PreparationReady', {}, requestId),
                  ...this.applyMatchProgress(lobby, match, match.start())];
              }
              break;
            }
            case 'PreparationFailed':
              lobby.abortPreparation(message.matchId!, message.payload.reason); this.matches.delete(message.matchId!);
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
      try { lobby = this.service.activeLobby(match.lobbyIdentity); }
      catch (error) {
        if (error instanceof LobbyError && error.code === 'LobbyNotFound') { this.matches.delete(match.matchId); continue; }
        throw error;
      }
      deliveries.push(...this.applyMatchProgress(lobby, match, match.tick()));
    }
    return deliveries;
  }

  beginPlaying(lobbyId: string, matchId: MatchId): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId); return this.broadcast(lobby.completePreparation(matchId), 'MatchStarted');
  }
  finish(lobbyId: string, matchId: MatchId, scores: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId), result = lobby.completeMatch(matchId, scores);
    this.matches.delete(matchId); return this.broadcast(lobby.state(), 'MatchResult', { result });
  }
  interrupt(lobbyId: string, matchId: MatchId, reason: string, scores?: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId), result = lobby.interruptMatch(matchId, reason, scores);
    this.matches.delete(matchId); return this.broadcast(lobby.state(), 'MatchResult', { result });
  }

  private applyMatchProgress(lobby: ReturnType<LobbyRegistry['activeLobby']>, match: MatchCoordinator,
    progress: MatchProgress): Delivery[] {
    const gameplay = progress.deliveries.map(delivery => ({
      connectionId: lobby.member(delivery.memberId).connectionId, message: delivery.message, visual: delivery.visual,
    }));
    const deliveries: Delivery[] = [];
    if (progress.becamePlaying) deliveries.push(...this.broadcast(lobby.completePreparation(match.matchId), 'MatchStarted'));
    deliveries.push(...gameplay);
    if (progress.completedScores) {
      const result = lobby.completeMatch(match.matchId, progress.completedScores); this.matches.delete(match.matchId);
      deliveries.push(...this.broadcast(lobby.state(), 'MatchResult', { result }));
    } else if (progress.interruptedReason) {
      const result = lobby.interruptMatch(match.matchId, progress.interruptedReason); this.matches.delete(match.matchId);
      deliveries.push(...this.broadcast(lobby.state(), 'MatchResult', { result }));
    }
    return deliveries;
  }

  private departureDeliveries(departure: Departure | undefined, requestId?: string): Delivery[] {
    if (!departure) return [];
    if (departure.matchId) this.matches.delete(departure.matchId);
    const deliveries: Delivery[] = [];
    if (departure.activeMatchInterrupted && departure.state) {
      const payload = { state: publicState(departure.state), result: departure.state.latestResult } as JsonObject;
      deliveries.push(...departure.recipients.map(connectionId => ({ connectionId,
        message: envelope('MatchResult', payload, { requestId, lobbyId: departure.lobbyId, matchId: departure.matchId }) })));
    } else if (!departure.closed && departure.state) {
      const payload = { state: publicState(departure.state) };
      deliveries.push(...departure.recipients.map(connectionId => ({ connectionId,
        message: envelope('LobbyState', payload, { requestId, lobbyId: departure.lobbyId, matchId: departure.matchId }) })));
    }
    if (departure.closed) deliveries.push(...departure.recipients.map(connectionId => ({ connectionId,
      message: envelope('LobbyClosed', { reason: departure.reason },
        { requestId, lobbyId: departure.lobbyId, matchId: departure.matchId }) })));
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
      { code, message, supportedProtocolVersion: PROTOCOL_VERSION }, { requestId }) };
  }
}
