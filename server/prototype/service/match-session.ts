// SPDX-License-Identifier: GPL-2.0-or-later
import { envelope as legacyEnvelope } from '../protocol.ts';
import type { Message, Role } from '../protocol.ts';
import type { Envelope, JsonObject } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import type { MemberId } from '../protocol/ids.ts';
import { Session } from '../session.ts';
import type { Peer } from '../session.ts';
import type { FrozenPlayer, LobbyState, MatchView } from './lobby.ts';

export interface MatchDelivery { memberId: MemberId; message: Envelope; visual?: boolean }
export interface MatchProgress {
  deliveries: MatchDelivery[];
  becamePlaying: boolean;
  completedScores?: number[][];
  interruptedReason?: string;
}

/**
 * Match-scoped v2 adapter around the proven authoritative v1 state machine.
 * It translates member/player identity at the boundary; the Kolf engine still
 * uses the frozen roster's engine indices internally.
 */
export class MatchSession {
  private readonly legacy: Session;
  private readonly match: MatchView;
  private readonly lobbyId: string;
  private readonly players = new Map<string, FrozenPlayer>();
  private readonly members = new Map<MemberId, { role: Role; peer: Peer }>();
  private outbox: MatchDelivery[] = [];
  private playingPublished = false;
  private terminalPublished = false;

  get matchId() { return this.match.matchId; }
  get lobbyIdentity() { return this.lobbyId; }

  constructor(state: LobbyState, now = () => Date.now()) {
    if (!state.match?.compatibilityId || state.match.roster.length !== 2)
      throw Error('match preparation is incomplete');
    this.match = state.match;
    this.lobbyId = state.lobbyId;
    this.legacy = new Session({ authority: state.match.compatibilityId, guest: state.match.compatibilityId }, now);
    for (const player of state.match.roster) this.players.set(player.playerId, player);
  }

  start(): MatchProgress {
    for (const player of this.match.roster) {
      const role: Role = player.memberId === this.match.authorityMemberId ? 'authority' : 'guest';
      const peer: Peer = {
        role,
        send: (message, visual) => this.send(player.memberId, message, visual),
        close: () => {},
      };
      this.members.set(player.memberId, { role, peer });
      this.legacy.join(peer, legacyEnvelope('Hello', {
        role, credential: this.match.compatibilityId, buildId: this.match.compatibilityId,
        courseHash: this.match.course.expectedHash,
      }));
    }
    return this.progress();
  }

  receive(memberId: MemberId, message: Envelope): MatchProgress {
    const member = this.members.get(memberId);
    if (!member) throw Error('member is not part of the frozen match roster');
    try {
      this.legacy.receiveMessage(member.peer, this.toLegacy(memberId, message));
    } catch {
      this.legacy.interrupt('authoritative match protocol failed');
    }
    return this.progress();
  }

  tick(): MatchProgress {
    this.legacy.tick();
    return this.progress();
  }

  private toLegacy(memberId: MemberId, message: Envelope): Message {
    const payload = { ...message.payload } as Message;
    let type = message.type;
    if (type === 'SceneReady') type = 'CourseReady';
    if (type === 'SubmitShot') {
      const player = this.players.get(String(payload.playerId));
      payload.playerSlot = player?.engineIndex ?? -1;
      delete payload.playerId;
    }
    return legacyEnvelope(type, payload);
  }

  private send(memberId: MemberId, message: Message, visual = false) {
    const { type, v: _version, matchId: _legacyMatchId, ...legacyPayload } = message;
    const payload = { ...legacyPayload } as JsonObject;
    if (type === 'Welcome') {
      const player = this.match.roster.find(entry => entry.memberId === memberId)!;
      delete payload.playerSlot;
      payload.playerId = player.playerId;
    }
    if (type === 'AdmitShot') {
      const player = this.match.roster.find(entry => entry.engineIndex === Number(payload.playerSlot));
      delete payload.playerSlot;
      if (player) payload.playerId = player.playerId;
    }
    this.outbox.push({ memberId, visual, message: envelope(String(type), payload, {
      lobbyId: this.lobbyId, matchId: this.match.matchId,
    }) });
  }

  private progress(): MatchProgress {
    const deliveries = this.outbox;
    this.outbox = [];
    const state = this.legacy.state;
    const becamePlaying = !this.playingPublished && !!state && !this.legacy.barrier && state.phase !== 'Finished';
    if (becamePlaying) this.playingPublished = true;
    let completedScores: number[][] | undefined;
    if (!this.terminalPublished && state?.phase === 'Finished' && !this.legacy.barrier) {
      this.terminalPublished = true;
      completedScores = state.scores as number[][];
    }
    let interruptedReason: string | undefined;
    if (!this.terminalPublished && this.legacy.interrupted) {
      this.terminalPublished = true;
      interruptedReason = 'The match protocol was interrupted.';
    }
    return { deliveries, becamePlaying, completedScores, interruptedReason };
  }
}
