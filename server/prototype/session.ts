// SPDX-License-Identifier: GPL-2.0-or-later
import { decode, envelope, id, integer, shotKey, validShot, validState } from './protocol.ts';
import type { Message, Role } from './protocol.ts';

export interface Peer { role: Role; send(message: Message, visual?: boolean): void; close(): void }
type Admission = { key: string; role: Role; status: Message };

/** One ordered fixed match. All methods run on Node's event loop. No golf rules. */
export class Session {
  peers = new Map<Role, Peer>();
  ready = new Map<Role, Message>();
  state?: Message;
  eventSeq = 0;
  syncId = 0;
  interrupted = false;
  barrier = true;
  private barrierPublished = false;
  pending?: string;
  pendingAt = 0;
  choicePending?: string;
  admissions = new Map<string, Admission>();
  private lastCommit = '';
  private lastFrame = 0;
  private resyncRound = 0;
  private nextResyncAt = 0;
  private queuedResync = false;
  private started = false;
  readonly credentials: Record<Role, string>;
  readonly now: () => number;
  constructor(credentials: Record<Role, string>, now = () => Date.now()) { this.credentials = credentials; this.now = now; }

  join(peer: Peer, hello: Message) {
    if (this.interrupted || this.peers.has(peer.role) || hello.type !== 'Hello'
      || hello.role !== peer.role || hello.credential !== this.credentials[peer.role]
      || !id(hello.buildId) || !/^[a-f0-9]{64}$/.test(hello.courseHash)) throw Error('admission denied');
    const other = this.ready.values().next().value;
    if (other && (other.buildId !== hello.buildId || other.courseHash !== hello.courseHash)) throw Error('build/course mismatch');
    this.peers.set(peer.role, peer);
    this.ready.set(peer.role, { buildId: hello.buildId, courseHash: hello.courseHash });
    peer.send(envelope('Welcome', { playerSlot: peer.role === 'authority' ? 0 : 1 }));
    if (this.peers.size === 2) this.broadcast(envelope('LoadCourse'));
  }
  broadcast(m: Message, visual = false) { for (const p of this.peers.values()) p.send(m, visual); }
  interrupt(reason: string) {
    if (this.interrupted) return;
    this.interrupted = true;
    this.broadcast(envelope('MatchInterrupted', { reason, eventSeq: ++this.eventSeq }));
  }
  disconnect(peer: Peer) {
    if (this.peers.get(peer.role) !== peer) return;
    this.peers.delete(peer.role);
    this.interrupt(`${peer.role} disconnected`);
  }
  tick() {
    if (!this.interrupted && this.queuedResync && this.now() >= this.nextResyncAt) this.startResync(this.now());
    if (this.pending && this.now() - this.pendingAt > 300_000) this.interrupt('shot/choice watchdog expired');
    if (this.barrier && this.state && this.now() - this.pendingAt > 30_000) this.interrupt('state application timed out');
  }
  private startResync(now: number) {
    this.queuedResync = false;
    this.barrier = true; this.barrierPublished = false; ++this.syncId; this.resyncRound = this.syncId;
    this.nextResyncAt = now + 1000; this.lastFrame = 0; this.pendingAt = now;
    for (const entry of this.ready.values()) entry.applied = 0;
    this.peers.get('authority')!.send(envelope('RequestResync', { syncId: this.syncId }));
  }
  receive(peer: Peer, raw: string) {
    this.receiveMessage(peer, decode(raw));
  }
  receiveMessage(peer: Peer, m: Message) {
    if (this.interrupted || this.peers.get(peer.role) !== peer) return;
    const reject = (reason: string) => peer.send(envelope('CommandRejected', { commandId: m.commandId, reason }));
    switch (m.type) {
      case 'CourseReady': {
        if (!/^[a-f0-9]{64}$/.test(m.manifestHash)) throw Error('invalid manifest');
        const entry = this.ready.get(peer.role)!;
        if (entry.manifestHash) {
          if (entry.manifestHash !== m.manifestHash) throw Error('manifest changed during loading');
          return;
        }
        entry.manifestHash = m.manifestHash;
        if (!this.started && [...this.ready.values()].every(r => r.manifestHash) && this.ready.size === 2) {
          const [a, b] = [...this.ready.values()];
          if (a.manifestHash !== b.manifestHash) return this.interrupt('loaded manifest mismatch');
          this.started = true;
          this.peers.get('authority')!.send(envelope('StartMatch'));
        }
        return;
      }
      case 'SubmitShot': {
        if (!validShot(m)) return reject('invalid shot');
        const known = this.admissions.get(m.commandId);
        if (known) {
          if (known.key !== shotKey(m) || known.role !== peer.role) return reject('command ID reused');
          peer.send(known.status); return;
        }
        const s = this.state;
        if (!s || this.barrier || this.pending || s.phase !== 'AwaitingShot') return reject('not awaiting shot');
        if (m.playerSlot !== (peer.role === 'authority' ? 0 : 1) || m.playerSlot !== s.activeSlot) return reject('wrong owner');
        if (m.turnId !== s.turnId || m.holeGeneration !== s.holeGeneration) return reject('stale turn');
        if (this.admissions.size >= 10000) return this.interrupt('command retention limit reached');
        this.pending = m.commandId; this.pendingAt = this.now();
        const status = envelope('ShotPending', { commandId: m.commandId });
        this.admissions.set(m.commandId, { key: shotKey(m), role: peer.role, status });
        peer.send(status);
        this.peers.get('authority')!.send(envelope('AdmitShot', m));
        return;
      }
      case 'ShotAccepted': case 'ShotRejected': {
        if (peer.role !== 'authority' || !this.admissions.has(m.commandId)) throw Error('invalid shot response');
        const admission = this.admissions.get(m.commandId)!;
        if (admission.status.type === 'ShotResolved') return;
        if (m.commandId !== this.pending) throw Error('invalid shot response');
        if (admission.status.type === m.type) return;
        admission.status = envelope(m.type, { commandId: m.commandId, reason: m.reason });
        this.broadcast(admission.status);
        if (m.type === 'ShotRejected') this.pending = undefined;
        return;
      }
      case 'InitialState': case 'CommitTransition': {
        if (peer.role !== 'authority' || !validState(m.state)) throw Error('invalid authority state');
        const key = JSON.stringify(m.state);
        if (this.state && m.state.stateRevision === this.state.stateRevision) {
          if (key !== this.lastCommit) throw Error('conflicting revision');
          peer.send(envelope('TransitionCommitted', { state: this.state, eventSeq: this.eventSeq, syncId: this.syncId })); return;
        }
        if (m.state.stateRevision !== (this.state?.stateRevision ?? 0) + 1) throw Error('revision gap');
        if (!this.started) throw Error('state before loading barrier');
        if (!this.state && (m.type !== 'InitialState' || m.state.phase !== 'AwaitingShot' || m.state.turnId !== 1 || m.state.holeGeneration !== 1 || m.state.hole !== 1)) throw Error('invalid initial state');
        if (this.state) {
          const before = this.state;
          const allowed: Record<string, string[]> = {
            AwaitingShot: ['Simulating'], Simulating: ['AwaitingHazardChoice', 'AwaitingShot', 'Finished'],
            AwaitingHazardChoice: ['AwaitingHazardChoice', 'Simulating', 'AwaitingShot', 'Finished'], Finished: [],
          };
          if (!this.pending || !allowed[before.phase]?.includes(m.state.phase)) throw Error('illegal phase transition');
          if (m.state.turnId !== before.turnId + (m.state.phase === 'AwaitingShot' ? 1 : 0)) throw Error('invalid turn progression');
          const holeDelta = m.state.hole - before.hole;
          if (holeDelta < 0 || holeDelta > 1 || m.state.holeGeneration !== before.holeGeneration + holeDelta) throw Error('invalid generation progression');
        }
        if (this.state && (m.state.turnId < this.state.turnId || m.state.holeGeneration < this.state.holeGeneration)) throw Error('regressed state');
        const wasPending = this.pending;
        this.state = m.state; this.lastCommit = key; this.lastFrame = 0;
        this.queuedResync = false;
        this.barrier = true; this.barrierPublished = true; ++this.syncId;
        this.pendingAt = this.now(); this.choicePending = undefined;
        for (const entry of this.ready.values()) entry.applied = 0;
        if (['AwaitingShot', 'Finished'].includes(m.state.phase) && wasPending) {
          this.admissions.get(wasPending)!.status = envelope('ShotResolved', { commandId: wasPending, stateRevision: m.state.stateRevision });
          this.pending = undefined;
        }
        this.broadcast(envelope('TransitionCommitted', { state: m.state, eventSeq: ++this.eventSeq, syncId: this.syncId }));
        return;
      }
      case 'StateApplied': {
        if (this.state && ((integer(m.syncId, 1) && m.syncId < this.syncId) || m.stateRevision < this.state.stateRevision)) return;
        if (!this.state || !integer(m.syncId, 1) || m.syncId !== this.syncId || m.stateRevision !== this.state.stateRevision
          || m.manifestHash !== this.state.manifestHash) throw Error('state acknowledgement mismatch');
        if (!this.barrier || !this.barrierPublished) return;
        this.ready.get(peer.role)!.applied = m.syncId;
        if ([...this.ready.values()].every(r => r.applied === this.syncId)) {
          this.barrier = false;
          this.broadcast(envelope('InputReady', { stateRevision: m.stateRevision, syncId: this.syncId }));
        }
        return;
      }
      case 'StateFrame': {
        if (peer.role !== 'authority' || !validState(m.state) || !integer(m.frameSeq, 1) || !integer(m.syncId, 1)) throw Error('invalid frame');
        if (!this.state || this.barrier || m.syncId !== this.syncId || m.state.stateRevision !== this.state.stateRevision
          || m.state.holeGeneration !== this.state.holeGeneration) return;
        this.checkMetadata(m.state);
        if (m.frameSeq <= this.lastFrame) return;
        this.lastFrame = m.frameSeq;
        this.peers.get('guest')?.send(m, true); return;
      }
      case 'ChooseHazardAction': {
        const s = this.state;
        if (!s || this.barrier || s.phase !== 'AwaitingHazardChoice' || s.choiceSlot !== (peer.role === 'authority' ? 0 : 1)
          || m.choiceId !== s.choiceId || m.stateRevision !== s.stateRevision || m.syncId !== this.syncId
          || !['drop', 'rehit'].includes(m.action)) return reject('invalid hazard choice');
        if (this.choicePending === m.choiceId) return;
        this.choicePending = m.choiceId;
        this.peers.get('authority')!.send(envelope('AdmitHazardAction', m)); return;
      }
      case 'RequestResync':
        if (this.state) {
          // Coalesce every request until the full-state barrier has completed.
          // A request during cooldown is queued so its client cannot wait forever.
          if (this.queuedResync || (this.barrier && this.resyncRound === this.syncId)) return;
          const now = this.now();
          if (now < this.nextResyncAt) {
            this.queuedResync = true; this.barrier = true; this.barrierPublished = false; this.pendingAt = now;
            for (const entry of this.ready.values()) entry.applied = 0;
          } else this.startResync(now);
        }
        return;
      case 'FullState':
        if (peer.role !== 'authority' || !validState(m.state) || !integer(m.syncId, 1)) throw Error('invalid resync');
        if (this.queuedResync) return;
        if (m.syncId < this.syncId) return;
        if (!this.barrier || m.syncId !== this.syncId || m.state.stateRevision !== this.state?.stateRevision) throw Error('invalid resync');
        this.checkMetadata(m.state);
        this.barrierPublished = true;
        this.broadcast(envelope('FullState', { state: m.state, eventSeq: this.eventSeq, syncId: this.syncId })); return;
      case 'MatchInterrupted': return this.interrupt('client reported invalid state');
      default: throw Error('unexpected message');
    }
  }
  private checkMetadata(state: Message) {
    for (const key of ['stateRevision', 'holeGeneration', 'turnId', 'activeSlot', 'hole', 'par', 'phase', 'manifestHash', 'courseHash', 'scores', 'choiceId', 'choiceSlot'])
      if (JSON.stringify(state[key]) !== JSON.stringify(this.state?.[key])) throw Error('uncommitted metadata change');
  }
}
