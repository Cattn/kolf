// SPDX-License-Identifier: GPL-2.0-or-later
import { decodeClientMessage } from '../protocol/codecs.ts';
import { envelope, ProtocolError } from '../protocol/envelope.ts';
import type { Envelope, JsonObject } from '../protocol/envelope.ts';
import type { ConnectionId, IdFactory, MatchId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import { LobbyError } from './errors.ts';
import type { CourseCatalogEntry, LobbyState } from './lobby.ts';
import { LobbyService } from './lobby-service.ts';

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
              if (result.allReady) return this.broadcast(lobby.state(), 'PreparationReady', {}, requestId);
              break;
            }
            case 'PreparationFailed':
              lobby.abortPreparation(message.matchId!, message.payload.reason);
              return this.broadcast(lobby.state(), 'PreparationAborted', { reason: message.payload.reason }, requestId);
            case 'ReturnToLobby': lobby.returnToLobby(memberId, message.requestId!, message.matchId!); break;
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

  beginPlaying(lobbyId: string, matchId: MatchId): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    return this.broadcast(lobby.completePreparation(matchId), 'MatchStarted');
  }

  finish(lobbyId: string, matchId: MatchId, scores: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    const result = lobby.completeMatch(matchId, scores);
    return this.broadcast(lobby.state(), 'MatchResult', { result });
  }

  interrupt(lobbyId: string, matchId: MatchId, reason: string, scores?: number[][]): Delivery[] {
    const lobby = this.service.activeLobby(lobbyId);
    const result = lobby.interruptMatch(matchId, reason, scores);
    return this.broadcast(lobby.state(), 'MatchResult', { result });
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
