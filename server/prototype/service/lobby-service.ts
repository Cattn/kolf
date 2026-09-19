// SPDX-License-Identifier: GPL-2.0-or-later
import type { ConnectionId, IdFactory, MemberId, RequestId } from '../protocol/ids.ts';
import { cryptoIds } from '../protocol/ids.ts';
import type { CourseCatalogEntry, MemberProfile } from './lobby.ts';
import { LobbySession } from './lobby.ts';
import { LobbyError } from './errors.ts';
import { RequestCache } from './request-cache.ts';

interface Membership { lobbyId: string; memberId: MemberId }

/** Single-lobby facade. A future registry can replace this without changing LobbySession. */
export class LobbyService {
  private lobby?: LobbySession;
  private readonly membership = new Map<ConnectionId, Membership>();
  private readonly requests: RequestCache;
  private readonly catalog: CourseCatalogEntry[];
  private readonly ids: IdFactory;
  private readonly now: () => number;

  constructor(catalog: CourseCatalogEntry[], ids: IdFactory = cryptoIds, now = () => Date.now()) {
    if (!catalog.length) throw new LobbyError('InvalidCatalog', 'at least one course is required');
    this.catalog = catalog; this.ids = ids; this.now = now;
    this.requests = new RequestCache(now);
  }

  create(connectionId: ConnectionId, requestId: RequestId, profile: MemberProfile, courseId: string) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'CreateLobby', { profile, courseId }, () => {
      if (this.lobby && this.lobby.phase !== 'Closed') throw new LobbyError('LobbyUnavailable', 'the development service already has a lobby');
      if (this.membership.has(connectionId)) throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
      const memberId = this.ids.member();
      this.lobby = new LobbySession({
        lobbyId: this.ids.lobby(), joinCode: this.ids.joinCode(), selectedCourseId: courseId,
        creator: { connectionId, memberId, playerId: this.ids.player(), ...profile },
        catalog: this.catalog, ids: this.ids, now: this.now,
      });
      this.membership.set(connectionId, { lobbyId: this.lobby.lobbyId, memberId });
      return { memberId, state: this.lobby.state() };
    });
  }

  join(connectionId: ConnectionId, requestId: RequestId, joinCode: string, profile: MemberProfile) {
    return this.requests.run(`connection:${connectionId}`, requestId, 'JoinLobby', { joinCode, profile }, () => {
      if (!this.lobby || this.lobby.phase === 'Closed' || this.lobby.joinCode !== joinCode)
        throw new LobbyError('LobbyNotFound', 'join code does not identify an open lobby');
      if (this.membership.has(connectionId)) throw new LobbyError('AlreadyJoined', 'connection already belongs to a lobby');
      const memberId = this.ids.member();
      const state = this.lobby.join({ connectionId, memberId, playerId: this.ids.player(), ...profile });
      this.membership.set(connectionId, { lobbyId: this.lobby.lobbyId, memberId });
      return { memberId, state };
    });
  }

  session(connectionId: ConnectionId): { lobby: LobbySession; memberId: MemberId } {
    const membership = this.membership.get(connectionId);
    if (!membership || !this.lobby || this.lobby.lobbyId !== membership.lobbyId)
      throw new LobbyError('NotMember', 'connection is not in a lobby');
    return { lobby: this.lobby, memberId: membership.memberId };
  }

  activeLobby(lobbyId: string): LobbySession {
    if (!this.lobby || this.lobby.lobbyId !== lobbyId || this.lobby.phase === 'Closed')
      throw new LobbyError('LobbyNotFound', 'lobby is not active');
    return this.lobby;
  }

  disconnect(connectionId: ConnectionId) {
    const membership = this.membership.get(connectionId);
    if (!membership || !this.lobby) return;
    this.membership.delete(connectionId);
    // The intentionally simple two-member policy closes the lobby. Active match
    // interruption is published by the caller before invoking disconnect.
    this.lobby.close();
    this.membership.clear();
  }
}
