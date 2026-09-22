// SPDX-License-Identifier: GPL-2.0-or-later
import type { ConnectionId, IdFactory, MemberId, RequestId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import type { CourseCatalogEntry, LobbyState, MemberProfile, PlayerProfile } from './lobby.ts';
import { LobbySession } from './lobby.ts';
import { LobbyError } from './errors.ts';
import { RequestCache } from './request-cache.ts';

interface Membership { lobbyId: string; memberId: MemberId }
export interface ServiceLimits {
  maximumConnections: number;
  maximumLobbies: number;
  maximumMembersPerLobby: number;
  maximumPlayersPerLobby: number;
  maximumRequestCacheEntries: number;
  maximumMessageBytes: number;
  maximumRetainedOutboundBytes: number;
}
export const DEFAULT_SERVICE_LIMITS: ServiceLimits = {
  maximumConnections: 64,
  maximumLobbies: 32,
  maximumMembersPerLobby: 8,
  maximumPlayersPerLobby: 8,
  maximumRequestCacheEntries: 256,
  maximumMessageBytes: 512 * 1024,
  maximumRetainedOutboundBytes: 1024 * 1024,
};

export interface Departure {
  lobbyId: string;
  matchId?: string;
  recipients: ConnectionId[];
  state?: LobbyState;
  closed: boolean;
  activeMatchInterrupted: boolean;
  reason: string;
}

/** Bounded indexes for every active v3 room and connection membership. */
export class LobbyRegistry {
  private readonly lobbiesById = new Map<string, LobbySession>();
  private readonly lobbyIdByJoinCode = new Map<string, string>();
  private readonly membershipByConnection = new Map<ConnectionId, Membership>();
  private readonly requests: RequestCache;
  readonly catalog: CourseCatalogEntry[];
  readonly limits: ServiceLimits;
  private readonly ids: IdFactory;
  private readonly now: () => number;

  constructor(catalog: CourseCatalogEntry[], ids: IdFactory = cryptoIds, now = () => Date.now(),
    limits: Partial<ServiceLimits> = {}) {
    if (!catalog.length) throw new LobbyError('InvalidCatalog', 'at least one course is required');
    this.catalog = catalog.map(course => ({ ...course, par: course.par ? [...course.par] : undefined }));
    this.ids = ids; this.now = now; this.limits = { ...DEFAULT_SERVICE_LIMITS, ...limits };
    if (this.limits.maximumLobbies < 1 || this.limits.maximumMembersPerLobby < 2
      || this.limits.maximumPlayersPerLobby < 2 || this.limits.maximumPlayersPerLobby > 8)
      throw new LobbyError('InvalidLimits', 'service limits are invalid');
    this.requests = new RequestCache(now, this.limits.maximumRequestCacheEntries);
  }

  get activeLobbyCount() { return this.lobbiesById.size; }
  get membershipCount() { return this.membershipByConnection.size; }

  create(connectionId: ConnectionId, requestId: RequestId, member: MemberProfile, player: PlayerProfile, courseId: string) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'CreateLobby', { member, player, courseId }, () => {
      if (this.membershipByConnection.has(connectionId)) throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
      if (this.lobbiesById.size >= this.limits.maximumLobbies) throw new LobbyError('ServiceFull', 'the service has reached its room limit');
      const joinCode = this.uniqueJoinCode(), memberId = this.ids.member(), lobbyId = this.ids.lobby();
      const lobby = new LobbySession({
        lobbyId, joinCode, selectedCourseId: courseId,
        creator: { connectionId, memberId, ...member },
        creatorPlayer: { playerId: this.ids.player(), ownerMemberId: memberId, order: 0, ...player },
        catalog: this.catalog, ids: this.ids, now: this.now,
        maximumMembers: this.limits.maximumMembersPerLobby,
        maximumPlayers: this.limits.maximumPlayersPerLobby,
        maximumRequests: this.limits.maximumRequestCacheEntries,
      });
      this.lobbiesById.set(lobbyId, lobby); this.lobbyIdByJoinCode.set(joinCode, lobbyId);
      this.membershipByConnection.set(connectionId, { lobbyId, memberId });
      return { memberId, state: lobby.state() };
    });
  }

  join(connectionId: ConnectionId, requestId: RequestId, joinCode: string, member: MemberProfile, player: PlayerProfile) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'JoinLobby', { joinCode, member, player }, () => {
      if (this.membershipByConnection.has(connectionId)) throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
      const lobbyId = this.lobbyIdByJoinCode.get(joinCode), lobby = lobbyId ? this.lobbiesById.get(lobbyId) : undefined;
      if (!lobby || lobby.phase === 'Closed') throw new LobbyError('LobbyNotFound', 'join code does not identify an open lobby');
      const memberId = this.ids.member();
      const state = lobby.join({ connectionId, memberId, ...member }, {
        playerId: this.ids.player(), ownerMemberId: memberId, order: lobby.playerCount, ...player,
      });
      this.membershipByConnection.set(connectionId, { lobbyId: lobby.lobbyId, memberId });
      return { memberId, state };
    });
  }

  session(connectionId: ConnectionId): { lobby: LobbySession; memberId: MemberId } {
    const membership = this.membershipByConnection.get(connectionId);
    const lobby = membership ? this.lobbiesById.get(membership.lobbyId) : undefined;
    if (!membership || !lobby || lobby.phase === 'Closed') throw new LobbyError('NotMember', 'connection is not in a lobby');
    return { lobby, memberId: membership.memberId };
  }

  activeLobby(lobbyId: string): LobbySession {
    const lobby = this.lobbiesById.get(lobbyId);
    if (!lobby || lobby.phase === 'Closed') throw new LobbyError('LobbyNotFound', 'lobby is not active');
    return lobby;
  }

  leave(connectionId: ConnectionId): Departure | undefined { return this.depart(connectionId, 'A participant left the lobby'); }
  disconnect(connectionId: ConnectionId): Departure | undefined { return this.depart(connectionId, 'A participant disconnected'); }

  private depart(connectionId: ConnectionId, reason: string): Departure | undefined {
    const membership = this.membershipByConnection.get(connectionId);
    const lobby = membership ? this.lobbiesById.get(membership.lobbyId) : undefined;
    if (!membership || !lobby) return undefined;
    const before = lobby.state(), matchId = before.match?.matchId;
    this.membershipByConnection.delete(connectionId);
    this.requests.clearScope(`connection:${connectionId}`);
    let activeMatchInterrupted = false;
    if (matchId && (before.phase === 'Preparing' || before.phase === 'Playing')) {
      lobby.interruptMatch(matchId, reason); activeMatchInterrupted = true;
    }
    const ownerDeparted = membership.memberId === lobby.ownerMemberId;
    if (!ownerDeparted) {
      const state = lobby.removeMember(membership.memberId);
      const recipients = state.members.map(member => member.connectionId);
      if (state.phase === 'Closed') this.removeLobby(lobby);
      return { lobbyId: lobby.lobbyId, matchId, recipients, state, closed: state.phase === 'Closed', activeMatchInterrupted, reason };
    }
    const terminalState = activeMatchInterrupted ? lobby.state() : undefined;
    const recipients = before.members.filter(member => member.connectionId !== connectionId).map(member => member.connectionId);
    lobby.close(); this.removeLobby(lobby);
    return { lobbyId: lobby.lobbyId, matchId, recipients, state: terminalState, closed: true, activeMatchInterrupted, reason };
  }

  private removeLobby(lobby: LobbySession) {
    this.lobbiesById.delete(lobby.lobbyId); this.lobbyIdByJoinCode.delete(lobby.joinCode);
    for (const [connectionId, membership] of this.membershipByConnection)
      if (membership.lobbyId === lobby.lobbyId) this.membershipByConnection.delete(connectionId);
  }

  private uniqueJoinCode() {
    for (let attempt = 0; attempt < 16; ++attempt) {
      const candidate = this.ids.joinCode();
      if (!this.lobbyIdByJoinCode.has(candidate)) return candidate;
    }
    throw new LobbyError('JoinCodeUnavailable', 'could not allocate a unique join code');
  }
}
