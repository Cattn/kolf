// SPDX-License-Identifier: GPL-2.0-or-later
import { finite, integer, validRosterState } from '../protocol.ts';
import type { Envelope, JsonObject } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import type { MemberId } from '../protocol/ids.ts';
import type { FrozenPlayer, LobbyState, MatchView } from './lobby.ts';

export interface MatchDelivery { memberId: MemberId; message: Envelope; visual?: boolean }
export interface MatchProgress {
  deliveries: MatchDelivery[];
  becamePlaying: boolean;
  completedScores?: number[][];
  interruptedReason?: string;
}

type State = Record<string, any>;
type Ready = { manifestHash?: string; applied: number };
type Admission = { key: string; memberId: MemberId; status: Envelope };

/** Authoritative v3 match state machine keyed by member and player identities. */
export class MatchCoordinator {
  private readonly match: MatchView;
  private readonly lobbyId: string;
  private readonly playersById = new Map<string, FrozenPlayer>();
  private readonly playersByIndex = new Map<number, FrozenPlayer>();
  private readonly members = new Map<MemberId, Ready>();
  private readonly now: () => number;
  private outbox: MatchDelivery[] = [];
  private state?: State;
  private eventSeq = 0;
  private syncId = 0;
  private interrupted = false;
  private barrier = true;
  private barrierPublished = false;
  private pending?: string;
  private pendingAt = 0;
  private choicePending?: string;
  private admissions = new Map<string, Admission>();
  private lastCommit = '';
  private lastFrame = 0;
  private resyncRound = 0;
  private nextResyncAt = 0;
  private queuedResync = false;
  private started = false;
  private loadedManifestHash?: string;
  private playingPublished = false;
  private terminalPublished = false;

  get matchId() { return this.match.matchId; }
  get lobbyIdentity() { return this.lobbyId; }

  constructor(state: LobbyState, now = () => Date.now()) {
    if (!state.match?.compatibilityId || state.match.roster.length < 2 || state.match.roster.length > 8)
      throw Error('match preparation is incomplete');
    this.match = state.match; this.lobbyId = state.lobbyId; this.now = now;
    const memberIds = new Set(state.members.map(member => member.memberId));
    for (const player of this.match.roster) {
      if (!memberIds.has(player.ownerMemberId) || this.playersById.has(player.playerId)
        || this.playersByIndex.has(player.engineIndex) || player.engineIndex < 0 || player.engineIndex >= this.match.roster.length)
        throw Error('frozen roster is invalid');
      this.playersById.set(player.playerId, player); this.playersByIndex.set(player.engineIndex, player);
      if (!this.members.has(player.ownerMemberId)) this.members.set(player.ownerMemberId, { applied: 0 });
    }
    if (this.members.size !== state.members.length || !this.members.has(this.match.authorityMemberId))
      throw Error('frozen member roster is invalid');
  }

  start(): MatchProgress {
    for (const memberId of this.members.keys()) {
      const playerIds = this.match.roster.filter(player => player.ownerMemberId === memberId).map(player => player.playerId);
      this.send(memberId, 'Welcome', { playerIds, authority: memberId === this.match.authorityMemberId });
    }
    this.broadcast('LoadCourse');
    return this.progress();
  }

  receive(memberId: MemberId, message: Envelope): MatchProgress {
    if (!this.members.has(memberId)) throw Error('member is not part of the frozen match roster');
    try { this.receiveMessage(memberId, message); }
    catch { this.interrupt('authoritative match protocol failed'); }
    return this.progress();
  }

  tick(): MatchProgress {
    if (!this.interrupted && this.queuedResync && this.now() >= this.nextResyncAt) this.startResync(this.now());
    if (this.pending && this.now() - this.pendingAt > 300_000) this.interrupt('shot/choice watchdog expired');
    if (this.barrier && this.state && this.now() - this.pendingAt > 30_000) this.interrupt('state application timed out');
    return this.progress();
  }

  private receiveMessage(memberId: MemberId, message: Envelope) {
    if (this.interrupted) return;
    const p = message.payload as State;
    const reject = (reason: string) => this.send(memberId, 'CommandRejected', { commandId: p.commandId, reason });
    switch (message.type) {
      case 'SceneReady': {
        const entry = this.members.get(memberId)!;
        if (entry.manifestHash) {
          if (entry.manifestHash !== p.manifestHash) throw Error('manifest changed during loading');
          return;
        }
        if (this.loadedManifestHash && this.loadedManifestHash !== p.manifestHash) throw Error('loaded manifest mismatch');
        this.loadedManifestHash = p.manifestHash;
        entry.manifestHash = p.manifestHash;
        if (!this.started && [...this.members.values()].every(ready => ready.manifestHash)) {
          this.started = true; this.send(this.match.authorityMemberId, 'StartMatch');
        }
        return;
      }
      case 'SubmitShot': {
        const player = this.playersById.get(String(p.playerId));
        const maximum = p.puttingMode === 'normal' ? 55.5 / 8 : p.puttingMode === 'advanced' ? 66.7 / 8 : 0;
        const valid = !!player && typeof p.commandId === 'string' && integer(p.holeGeneration, 1) && integer(p.turnId, 1)
          && finite(p.directionRadians, -Math.PI, Math.PI) && finite(p.launchMagnitude, Number.MIN_VALUE, maximum);
        if (!valid) return reject('invalid shot');
        const key = JSON.stringify([p.holeGeneration, p.turnId, p.playerId, p.puttingMode, p.directionRadians, p.launchMagnitude]);
        const known = this.admissions.get(p.commandId);
        if (known) {
          if (known.key !== key || known.memberId !== memberId) return reject('command ID reused');
          this.outbox.push({ memberId, message: known.status }); return;
        }
        const active = this.playersByIndex.get(this.state?.activeSlot);
        if (!this.state || this.barrier || this.pending || this.state.phase !== 'AwaitingShot') return reject('not awaiting shot');
        if (player.ownerMemberId !== memberId || active?.playerId !== player.playerId) return reject('wrong owner');
        if (p.turnId !== this.state.turnId || p.holeGeneration !== this.state.holeGeneration) return reject('stale turn');
        if (this.admissions.size >= 10_000) { this.interrupt('command retention limit reached'); return; }
        this.pending = p.commandId; this.pendingAt = this.now();
        const status = envelope('ShotPending', { commandId: p.commandId }, { lobbyId: this.lobbyId, matchId: this.match.matchId });
        this.admissions.set(p.commandId, { key, memberId, status });
        this.outbox.push({ memberId, message: status });
        this.send(this.match.authorityMemberId, 'AdmitShot', { ...p });
        return;
      }
      case 'ShotAccepted': case 'ShotRejected': {
        if (memberId !== this.match.authorityMemberId || !this.admissions.has(p.commandId)) throw Error('invalid shot response');
        const admission = this.admissions.get(p.commandId)!;
        if (admission.status.type === 'ShotResolved') return;
        if (p.commandId !== this.pending) throw Error('invalid shot response');
        if (admission.status.type === message.type) return;
        admission.status = envelope(message.type, { commandId: p.commandId, reason: p.reason },
          { lobbyId: this.lobbyId, matchId: this.match.matchId });
        this.broadcastEnvelope(admission.status);
        if (message.type === 'ShotRejected') this.pending = undefined;
        return;
      }
      case 'InitialState': case 'CommitTransition': {
        if (memberId !== this.match.authorityMemberId || !validRosterState(p.state, this.match.roster.length)
          || (!this.state && p.state.manifestHash !== this.loadedManifestHash))
          throw Error('invalid authority state');
        const next = p.state as State, key = JSON.stringify(next);
        if (this.state && next.stateRevision === this.state.stateRevision) {
          if (key !== this.lastCommit) throw Error('conflicting revision');
          this.send(memberId, 'TransitionCommitted', { state: this.state, eventSeq: this.eventSeq, syncId: this.syncId }); return;
        }
        if (next.stateRevision !== (this.state?.stateRevision ?? 0) + 1) throw Error('revision gap');
        if (!this.started) throw Error('state before loading barrier');
        if (!this.state && (message.type !== 'InitialState' || next.phase !== 'AwaitingShot' || next.turnId !== 1
          || next.holeGeneration !== 1 || next.hole !== 1)) throw Error('invalid initial state');
        if (this.state) {
          const allowed: Record<string, string[]> = {
            AwaitingShot: ['Simulating'], Simulating: ['AwaitingHazardChoice', 'AwaitingShot', 'Finished'],
            AwaitingHazardChoice: ['AwaitingHazardChoice', 'Simulating', 'AwaitingShot', 'Finished'], Finished: [],
          };
          if (!this.pending || !allowed[this.state.phase]?.includes(next.phase)) throw Error('illegal phase transition');
          if (next.turnId !== this.state.turnId + (next.phase === 'AwaitingShot' ? 1 : 0)) throw Error('invalid turn progression');
          const holeDelta = next.hole - this.state.hole;
          if (holeDelta < 0 || holeDelta > 1 || next.holeGeneration !== this.state.holeGeneration + holeDelta)
            throw Error('invalid generation progression');
        }
        const wasPending = this.pending;
        this.state = next; this.lastCommit = key; this.lastFrame = 0; this.queuedResync = false;
        this.barrier = true; this.barrierPublished = true; ++this.syncId; this.pendingAt = this.now(); this.choicePending = undefined;
        for (const entry of this.members.values()) entry.applied = 0;
        if (['AwaitingShot', 'Finished'].includes(next.phase) && wasPending) {
          this.admissions.get(wasPending)!.status = envelope('ShotResolved',
            { commandId: wasPending, stateRevision: next.stateRevision }, { lobbyId: this.lobbyId, matchId: this.match.matchId });
          this.pending = undefined;
        }
        this.broadcast('TransitionCommitted', { state: next, eventSeq: ++this.eventSeq, syncId: this.syncId });
        return;
      }
      case 'StateApplied': {
        if (this.state && ((integer(p.syncId, 1) && p.syncId < this.syncId) || p.stateRevision < this.state.stateRevision)) return;
        if (!this.state || !integer(p.syncId, 1) || p.syncId !== this.syncId || p.stateRevision !== this.state.stateRevision
          || p.manifestHash !== this.state.manifestHash) throw Error('state acknowledgement mismatch');
        if (!this.barrier || !this.barrierPublished) return;
        this.members.get(memberId)!.applied = p.syncId;
        if ([...this.members.values()].every(ready => ready.applied === this.syncId)) {
          this.barrier = false; this.broadcast('InputReady', { stateRevision: p.stateRevision, syncId: this.syncId });
        }
        return;
      }
      case 'StateFrame': {
        if (memberId !== this.match.authorityMemberId || !validRosterState(p.state, this.match.roster.length)
          || !integer(p.frameSeq, 1) || !integer(p.syncId, 1)) throw Error('invalid frame');
        if (!this.state || this.barrier || p.syncId !== this.syncId || p.state.stateRevision !== this.state.stateRevision
          || p.state.holeGeneration !== this.state.holeGeneration) return;
        this.checkMetadata(p.state);
        if (p.frameSeq <= this.lastFrame) return;
        this.lastFrame = p.frameSeq;
        for (const target of this.members.keys()) if (target !== this.match.authorityMemberId)
          this.send(target, 'StateFrame', { ...p }, true);
        return;
      }
      case 'ChooseHazardAction': {
        const player = this.playersByIndex.get(this.state?.choiceSlot);
        if (!this.state || this.barrier || this.state.phase !== 'AwaitingHazardChoice' || player?.ownerMemberId !== memberId
          || p.choiceId !== this.state.choiceId || p.stateRevision !== this.state.stateRevision || p.syncId !== this.syncId
          || !['drop', 'rehit'].includes(p.action)) return reject('invalid hazard choice');
        if (this.choicePending === p.choiceId) return;
        this.choicePending = p.choiceId; this.send(this.match.authorityMemberId, 'AdmitHazardAction', { ...p }); return;
      }
      case 'RequestResync':
        if (this.state) {
          if (this.queuedResync || (this.barrier && this.resyncRound === this.syncId)) return;
          const now = this.now();
          if (now < this.nextResyncAt) {
            this.queuedResync = true; this.barrier = true; this.barrierPublished = false; this.pendingAt = now;
            for (const entry of this.members.values()) entry.applied = 0;
          } else this.startResync(now);
        }
        return;
      case 'FullState':
        if (memberId !== this.match.authorityMemberId || !validRosterState(p.state, this.match.roster.length)
          || !integer(p.syncId, 1)) throw Error('invalid resync');
        if (this.queuedResync || p.syncId < this.syncId) return;
        if (!this.barrier || p.syncId !== this.syncId || p.state.stateRevision !== this.state?.stateRevision)
          throw Error('invalid resync');
        this.checkMetadata(p.state); this.barrierPublished = true;
        this.broadcast('FullState', { state: p.state, eventSeq: this.eventSeq, syncId: this.syncId }); return;
      case 'MatchInterrupted': this.interrupt('client reported invalid state'); return;
      default: throw Error('unexpected message');
    }
  }

  private startResync(now: number) {
    this.queuedResync = false; this.barrier = true; this.barrierPublished = false; ++this.syncId; this.resyncRound = this.syncId;
    this.nextResyncAt = now + 1000; this.lastFrame = 0; this.pendingAt = now;
    for (const entry of this.members.values()) entry.applied = 0;
    this.send(this.match.authorityMemberId, 'RequestResync', { syncId: this.syncId });
  }

  private interrupt(reason: string) {
    if (this.interrupted) return;
    this.interrupted = true; this.broadcast('MatchInterrupted', { reason, eventSeq: ++this.eventSeq });
  }

  private checkMetadata(state: State) {
    for (const key of ['stateRevision', 'holeGeneration', 'turnId', 'activeSlot', 'hole', 'par', 'phase', 'manifestHash',
      'courseHash', 'scores', 'choiceId', 'choiceSlot'])
      if (JSON.stringify(state[key]) !== JSON.stringify(this.state?.[key])) throw Error('uncommitted metadata change');
  }

  private send(memberId: MemberId, type: string, payload: JsonObject = {}, visual = false) {
    this.outbox.push({ memberId, visual, message: envelope(type, payload,
      { lobbyId: this.lobbyId, matchId: this.match.matchId }) });
  }
  private broadcast(type: string, payload: JsonObject = {}) {
    const message = envelope(type, payload, { lobbyId: this.lobbyId, matchId: this.match.matchId });
    this.broadcastEnvelope(message);
  }
  private broadcastEnvelope(message: Envelope) {
    for (const memberId of this.members.keys()) this.outbox.push({ memberId, message });
  }

  private progress(): MatchProgress {
    const deliveries = this.outbox; this.outbox = [];
    const becamePlaying = !this.playingPublished && !!this.state && !this.barrier && this.state.phase !== 'Finished';
    if (becamePlaying) this.playingPublished = true;
    let completedScores: number[][] | undefined;
    if (!this.terminalPublished && this.state?.phase === 'Finished' && !this.barrier) {
      this.terminalPublished = true; completedScores = this.state.scores as number[][];
    }
    let interruptedReason: string | undefined;
    if (!this.terminalPublished && this.interrupted) {
      this.terminalPublished = true; interruptedReason = 'The match protocol was interrupted.';
    }
    return { deliveries, becamePlaying, completedScores, interruptedReason };
  }
}

export { MatchCoordinator as MatchSession };
