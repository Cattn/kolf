// SPDX-License-Identifier: GPL-2.0-or-later
import type { ConnectionId, IdFactory, MemberId, RequestId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import type { CourseCatalogEntry, LobbyState, MemberProfile, PlayerProfile } from './lobby.ts';
import { LobbySession } from './lobby.ts';
import { randomBytes } from 'node:crypto';
import { LobbyError } from './errors.ts';
import type { ClientInfo } from '../protocol/codecs.ts';
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
  lobbyIdleMs: number;
  maximumMatchMs: number;
  lobbyReconnectMs: number;
  matchReconnectMs: number;
}
export const DEFAULT_SERVICE_LIMITS: ServiceLimits = {
  maximumConnections: 64,
  maximumLobbies: 32,
  maximumMembersPerLobby: 8,
  maximumPlayersPerLobby: 8,
  maximumRequestCacheEntries: 256,
  maximumMessageBytes: 512 * 1024,
  maximumRetainedOutboundBytes: 1024 * 1024,
  lobbyIdleMs: 30 * 60 * 1000,
  maximumMatchMs: 3 * 60 * 60 * 1000,
  lobbyReconnectMs: 120 * 1000,
  matchReconnectMs: 60 * 1000,
};

export interface Departure {
  lobbyId: string;
  matchId?: string;
  recipients: ConnectionId[];
  state?: LobbyState;
  closed: boolean;
  activeMatchInterrupted: boolean;
  reason: string;
  memberId?: MemberId;
}

/** Bounded indexes for every active v3 room and connection membership. */
export class LobbyRegistry {
  private readonly lobbiesById = new Map<string, LobbySession>();
  private readonly lobbyIdByJoinCode = new Map<string, string>();
  private readonly membershipByConnection = new Map<ConnectionId, Membership>();
  private readonly lastActivity = new Map<string, number>();
  private readonly hostBuilds = new Map<string, ClientInfo>();
  private readonly resumeTokens = new Map<string, { lobbyId: string; memberId: MemberId }>();
  private readonly tokenByMember = new Map<string, string>();
  private readonly reconnecting = new Map<string, { lobbyId: string; memberId: MemberId; deadline: number }>();
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
  get activeMatchLobbyCount() {
    let count = 0;
    for (const lobby of this.lobbiesById.values()) if (lobby.phase === 'Preparing' || lobby.phase === 'Playing') ++count;
    return count;
  }

  expireUploads() { for (const lobby of this.lobbiesById.values()) lobby.transfers.expire(); }

  create(connectionId: ConnectionId, requestId: RequestId, member: MemberProfile, player: PlayerProfile, courseId: string,
    clientInfo?: ClientInfo) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'CreateLobby', { member, player, courseId, clientInfo }, () => {
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
      this.lastActivity.set(lobbyId, this.now());
      if (clientInfo) this.hostBuilds.set(lobbyId, { ...clientInfo });
      this.membershipByConnection.set(connectionId, { lobbyId, memberId });
      return { memberId, state: lobby.state(), resumeToken: this.issueToken(lobbyId, memberId) };
    });
  }

  join(connectionId: ConnectionId, requestId: RequestId, joinCode: string, member: MemberProfile, player: PlayerProfile,
    clientInfo?: ClientInfo) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'JoinLobby', { joinCode, member, player, clientInfo }, () => {
      if (this.membershipByConnection.has(connectionId)) throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
      const lobbyId = this.lobbyIdByJoinCode.get(joinCode), lobby = lobbyId ? this.lobbiesById.get(lobbyId) : undefined;
      if (!lobby || lobby.phase === 'Closed') throw new LobbyError('LobbyNotFound', 'join code does not identify an open lobby');
      const hostBuild = this.hostBuilds.get(lobby.lobbyId);
      if (hostBuild && hostBuild.rulesBuildId !== clientInfo?.rulesBuildId)
        throw new LobbyError('IncompatibleBuild', 'this lobby was created with a different build of Kolf', {
          hostVersion: hostBuild.appVersion, hostBuild: hostBuild.rulesBuildId.slice(0, 8),
          yourVersion: clientInfo?.appVersion ?? 'unknown', yourBuild: clientInfo?.rulesBuildId.slice(0, 8) ?? 'unknown',
        });
      const memberId = this.ids.member();
      const state = lobby.join({ connectionId, memberId, ...member }, {
        playerId: this.ids.player(), ownerMemberId: memberId, order: lobby.playerCount, ...player,
      });
      this.membershipByConnection.set(connectionId, { lobbyId: lobby.lobbyId, memberId });
      this.lastActivity.set(lobby.lobbyId, this.now());
      return { memberId, state, resumeToken: this.issueToken(lobby.lobbyId, memberId) };
    });
  }

  session(connectionId: ConnectionId): { lobby: LobbySession; memberId: MemberId } {
    const membership = this.membershipByConnection.get(connectionId);
    const lobby = membership ? this.lobbiesById.get(membership.lobbyId) : undefined;
    if (!membership || !lobby || lobby.phase === 'Closed') throw new LobbyError('NotMember', 'connection is not in a lobby');
    this.lastActivity.set(lobby.lobbyId, this.now());
    return { lobby, memberId: membership.memberId };
  }

  activeLobby(lobbyId: string): LobbySession {
    const lobby = this.lobbiesById.get(lobbyId);
    if (!lobby || lobby.phase === 'Closed') throw new LobbyError('LobbyNotFound', 'lobby is not active');
    return lobby;
  }

  leave(connectionId: ConnectionId): Departure | undefined { return this.depart(connectionId, 'A participant left the lobby'); }

  disconnect(connectionId: ConnectionId): Departure | undefined {
    const membership = this.membershipByConnection.get(connectionId);
    const lobby = membership ? this.lobbiesById.get(membership.lobbyId) : undefined;
    const before = lobby?.state();
    const inMatch = !!lobby && lobby.phase === 'Playing' && !!membership;
    if (!membership || !lobby || (lobby.phase !== 'Open' && lobby.phase !== 'Results' && !inMatch))
      return this.depart(connectionId, 'A participant disconnected');
    this.membershipByConnection.delete(connectionId);
    this.requests.clearScope(`connection:${connectionId}`);
    const deadline = this.now() + (inMatch ? this.limits.matchReconnectMs : this.limits.lobbyReconnectMs);
    this.reconnecting.set(this.memberKey(lobby.lobbyId, membership.memberId),
      { lobbyId: lobby.lobbyId, memberId: membership.memberId, deadline });
    const state = lobby.markReconnecting(membership.memberId, deadline);
    const recipients = state.members.filter(member => member.memberId !== membership.memberId).map(member => member.connectionId);
    return { lobbyId: lobby.lobbyId, matchId: inMatch ? before?.match?.matchId : undefined, memberId: membership.memberId,
      recipients, state, closed: false, activeMatchInterrupted: false, reason: 'A participant is reconnecting' };
  }

  resume(connectionId: ConnectionId, resumeToken: string) {
    const entry = this.resumeTokens.get(resumeToken);
    const lobby = entry ? this.lobbiesById.get(entry.lobbyId) : undefined;
    if (!entry || !lobby || lobby.phase === 'Closed')
      throw new LobbyError('ResumeRejected', 'this session can no longer be resumed');
    const existing = this.membershipByConnection.get(connectionId);
    if (existing && (existing.lobbyId !== entry.lobbyId || existing.memberId !== entry.memberId))
      throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
    const key = this.memberKey(entry.lobbyId, entry.memberId);
    if (!this.reconnecting.has(key) && lobby.phase !== 'Open' && lobby.phase !== 'Results')
      throw new LobbyError('ResumeRejected', 'this session can no longer be resumed');
    const previous = lobby.member(entry.memberId).connectionId;
    let replacedConnectionId: ConnectionId | undefined;
    if (previous !== connectionId && this.membershipByConnection.get(previous)?.memberId === entry.memberId) {
      this.membershipByConnection.delete(previous);
      this.requests.clearScope(`connection:${previous}`);
      replacedConnectionId = previous;
    }
    this.reconnecting.delete(key);
    const state = lobby.rebind(entry.memberId, connectionId);
    this.membershipByConnection.set(connectionId, { lobbyId: entry.lobbyId, memberId: entry.memberId });
    this.lastActivity.set(entry.lobbyId, this.now());
    return { memberId: entry.memberId, state, resumeToken, replacedConnectionId };
  }

  expireReconnects(): Departure[] {
    const departures: Departure[] = [];
    for (const [key, entry] of [...this.reconnecting]) {
      if (entry.deadline > this.now()) continue;
      this.reconnecting.delete(key);
      const lobby = this.lobbiesById.get(entry.lobbyId);
      if (!lobby || lobby.phase === 'Closed') continue;
      const departure = this.departMember(lobby, entry.memberId, lobby.member(entry.memberId).connectionId, 'A participant disconnected');
      if (departure) departures.push(departure);
    }
    return departures;
  }

  private memberKey(lobbyId: string, memberId: MemberId) { return `${lobbyId}:${memberId}`; }

  private issueToken(lobbyId: string, memberId: MemberId): string {
    const key = this.memberKey(lobbyId, memberId);
    const existing = this.tokenByMember.get(key);
    if (existing) return existing;
    const token = randomBytes(16).toString('base64url');
    this.resumeTokens.set(token, { lobbyId, memberId });
    this.tokenByMember.set(key, token);
    return token;
  }

  private forgetMember(lobbyId: string, memberId: MemberId) {
    const key = this.memberKey(lobbyId, memberId);
    const token = this.tokenByMember.get(key);
    if (token) this.resumeTokens.delete(token);
    this.tokenByMember.delete(key); this.reconnecting.delete(key);
  }

  private depart(connectionId: ConnectionId, reason: string): Departure | undefined {
    const membership = this.membershipByConnection.get(connectionId);
    const lobby = membership ? this.lobbiesById.get(membership.lobbyId) : undefined;
    if (!membership || !lobby) return undefined;
    this.membershipByConnection.delete(connectionId);
    this.requests.clearScope(`connection:${connectionId}`);
    return this.departMember(lobby, membership.memberId, connectionId, reason);
  }

  private departMember(lobby: LobbySession, memberId: MemberId, connectionId: ConnectionId, reason: string): Departure | undefined {
    const membership = { lobbyId: lobby.lobbyId, memberId };
    this.forgetMember(lobby.lobbyId, memberId);
    const before = lobby.state(), matchId = before.match?.matchId;
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

  expireIdleLobbies(): Departure[] {
    const departures: Departure[] = [];
    const cutoff = this.now() - this.limits.lobbyIdleMs;
    for (const lobby of [...this.lobbiesById.values()]) {
      if (lobby.phase !== 'Open' || (this.lastActivity.get(lobby.lobbyId) ?? 0) > cutoff) continue;
      const recipients = lobby.state().members.map(member => member.connectionId);
      lobby.close(); this.removeLobby(lobby);
      departures.push({ lobbyId: lobby.lobbyId, recipients, closed: true, activeMatchInterrupted: false,
        reason: 'The lobby closed after 30 minutes without activity.' });
    }
    return departures;
  }

  private removeLobby(lobby: LobbySession) {
    this.lobbiesById.delete(lobby.lobbyId); this.lobbyIdByJoinCode.delete(lobby.joinCode);
    this.lastActivity.delete(lobby.lobbyId); this.hostBuilds.delete(lobby.lobbyId);
    for (const member of lobby.state().members) this.forgetMember(lobby.lobbyId, member.memberId);
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
