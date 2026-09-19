// SPDX-License-Identifier: GPL-2.0-or-later
import type { ConnectionId, IdFactory, JoinCode, LobbyId, MatchId, MemberId, PlayerId, RequestId } from '../protocol/ids.ts';
import { validators } from '../protocol/codecs.ts';
import { LobbyError } from './errors.ts';
import { RequestCache } from './request-cache.ts';

export type LobbyPhase = 'Open' | 'Preparing' | 'Playing' | 'Results' | 'Closed';

export interface CourseCatalogEntry {
  courseId: string;
  displayName: string;
  expectedHash: string;
  par?: number[];
}

export interface MemberProfile { displayName: string; color: string }
export interface LobbyMember extends MemberProfile {
  memberId: MemberId;
  connectionId: ConnectionId;
  playerId: PlayerId;
}
export interface FrozenPlayer extends MemberProfile {
  playerId: PlayerId;
  memberId: MemberId;
  engineIndex: number;
}
export interface MatchView {
  matchId: MatchId;
  authorityMemberId: MemberId;
  course: CourseCatalogEntry;
  roster: FrozenPlayer[];
  compatibilityId?: string;
}
export interface MatchResult {
  matchId: MatchId;
  courseId: string;
  courseHash: string;
  roster: FrozenPlayer[];
  scores: number[][];
  par: number[];
  totals: number[];
  winnerPlayerIds: PlayerId[];
  status: 'Completed' | 'Interrupted';
  reason?: string;
  completedAt: number;
}
export interface LobbyState {
  lobbyId: LobbyId;
  joinCode: JoinCode;
  ownerMemberId: MemberId;
  lobbyRevision: number;
  phase: LobbyPhase;
  selectedCourseId: string;
  members: Array<LobbyMember & { ready: boolean }>;
  match?: MatchView;
  latestResult?: MatchResult;
}

type MatchShell = MatchView & {
  courseReady: Map<MemberId, { courseHash: string; compatibilityId: string }>;
  returned: Set<MemberId>;
};

interface LobbyOptions {
  lobbyId: LobbyId;
  joinCode: JoinCode;
  creator: LobbyMember;
  selectedCourseId: string;
  catalog: CourseCatalogEntry[];
  ids: IdFactory;
  now?: () => number;
}

const cloneCourse = (course: CourseCatalogEntry): CourseCatalogEntry => ({ ...course, par: course.par ? [...course.par] : undefined });
const cloneRoster = (roster: FrozenPlayer[]) => roster.map(player => ({ ...player }));
const cloneMatch = (match: MatchShell): MatchView => ({
  matchId: match.matchId,
  authorityMemberId: match.authorityMemberId,
  course: cloneCourse(match.course),
  roster: cloneRoster(match.roster),
  compatibilityId: match.compatibilityId,
});
const cloneResult = (result: MatchResult): MatchResult => ({
  ...result,
  roster: cloneRoster(result.roster),
  scores: result.scores.map(row => [...row]),
  par: [...result.par], totals: [...result.totals], winnerPlayerIds: [...result.winnerPlayerIds],
});

/** Pure one-lobby domain model. Transport and gameplay simulation live elsewhere. */
export class LobbySession {
  readonly lobbyId: LobbyId;
  readonly joinCode: JoinCode;
  readonly ownerMemberId: MemberId;
  private readonly catalog = new Map<string, CourseCatalogEntry>();
  private readonly members = new Map<MemberId, LobbyMember>();
  private readonly readiness = new Map<MemberId, number>();
  private readonly requests: RequestCache;
  private readonly ids: IdFactory;
  private readonly now: () => number;
  private match?: MatchShell;
  private result?: MatchResult;
  private selectedCourseId: string;
  private revision = 1;
  private lifecycle: LobbyPhase = 'Open';

  constructor(options: LobbyOptions) {
    this.lobbyId = options.lobbyId;
    this.joinCode = options.joinCode;
    this.ownerMemberId = options.creator.memberId;
    this.ids = options.ids;
    this.now = options.now ?? (() => Date.now());
    this.requests = new RequestCache(this.now);
    for (const course of options.catalog) {
      if (!validators.hash(course.expectedHash) || !course.displayName || this.catalog.has(course.courseId))
        throw new LobbyError('InvalidCatalog', 'course catalog entry is invalid or duplicated');
      this.catalog.set(course.courseId, cloneCourse(course));
    }
    if (!this.catalog.has(options.selectedCourseId)) throw new LobbyError('UnknownCourse', 'selected course is not in the catalog');
    this.selectedCourseId = options.selectedCourseId;
    this.assertProfile(options.creator);
    this.members.set(options.creator.memberId, { ...options.creator });
  }

  get phase() { return this.lifecycle; }
  get lobbyRevision() { return this.revision; }
  get currentMatchId() { return this.match?.matchId; }
  get memberCount() { return this.members.size; }

  state(): LobbyState {
    return {
      lobbyId: this.lobbyId, joinCode: this.joinCode, ownerMemberId: this.ownerMemberId,
      lobbyRevision: this.revision, phase: this.lifecycle, selectedCourseId: this.selectedCourseId,
      members: [...this.members.values()].map(member => ({ ...member, ready: this.readiness.get(member.memberId) === this.revision })),
      match: this.match ? cloneMatch(this.match) : undefined,
      latestResult: this.result ? cloneResult(this.result) : undefined,
    };
  }

  member(memberId: MemberId): LobbyMember {
    const member = this.members.get(memberId);
    if (!member) throw new LobbyError('NotMember', 'connection is not a member of this lobby');
    return { ...member };
  }

  join(member: LobbyMember) {
    this.requirePhase('Open');
    if (this.members.size >= 2) throw new LobbyError('LobbyFull', 'this lobby already has two members');
    if ([...this.members.values()].some(existing => existing.connectionId === member.connectionId))
      throw new LobbyError('AlreadyJoined', 'connection is already in this lobby');
    this.assertProfile(member);
    this.members.set(member.memberId, { ...member });
    this.changed();
    return this.state();
  }

  updateMember(actor: MemberId, requestId: RequestId, patch: Partial<MemberProfile>) {
    return this.mutate(actor, requestId, 'UpdateMember', patch, () => {
      this.requirePhase('Open');
      const member = this.requireMember(actor);
      const next = { displayName: patch.displayName ?? member.displayName, color: patch.color ?? member.color };
      this.assertProfile(next);
      if (next.displayName !== member.displayName || next.color !== member.color) {
        Object.assign(member, next);
        this.changed();
      }
      return this.state();
    });
  }

  setCourse(actor: MemberId, requestId: RequestId, courseId: string) {
    return this.mutate(actor, requestId, 'SetCourse', { courseId }, () => {
      this.requireOwner(actor); this.requirePhase('Open');
      if (!this.catalog.has(courseId)) throw new LobbyError('UnknownCourse', 'course is not in the server catalog');
      if (courseId !== this.selectedCourseId) { this.selectedCourseId = courseId; this.changed(); }
      return this.state();
    });
  }

  setReady(actor: MemberId, requestId: RequestId, lobbyRevision: number, ready: boolean) {
    return this.mutate(actor, requestId, 'SetReady', { lobbyRevision, ready }, () => {
      this.requireMember(actor); this.requirePhase('Open'); this.requireRevision(lobbyRevision);
      if (ready) this.readiness.set(actor, this.revision); else this.readiness.delete(actor);
      return this.state();
    });
  }

  start(actor: MemberId, requestId: RequestId, lobbyRevision: number): MatchView {
    return this.mutate(actor, requestId, 'StartMatch', { lobbyRevision }, () => {
      this.requireOwner(actor); this.requireRevision(lobbyRevision);
      if (this.match && (this.lifecycle === 'Preparing' || this.lifecycle === 'Playing')) return cloneMatch(this.match);
      this.requirePhase('Open');
      if (this.members.size !== 2) throw new LobbyError('LobbyNotReady', 'two members are required');
      if ([...this.members].some(([memberId]) => this.readiness.get(memberId) !== this.revision))
        throw new LobbyError('LobbyNotReady', 'both members must be ready on the current revision');
      const course = this.catalog.get(this.selectedCourseId)!;
      const roster = [...this.members.values()].map((member, engineIndex) => ({
        memberId: member.memberId, playerId: member.playerId, engineIndex,
        displayName: member.displayName, color: member.color,
      }));
      this.match = {
        matchId: this.ids.match(), authorityMemberId: this.ownerMemberId, course: cloneCourse(course), roster,
        courseReady: new Map(), returned: new Set(),
      };
      this.lifecycle = 'Preparing';
      this.result = undefined;
      return cloneMatch(this.match);
    });
  }

  courseReady(actor: MemberId, requestId: RequestId, matchId: MatchId, courseHash: string, compatibilityId: string) {
    return this.mutate(actor, requestId, 'CourseReady', { matchId, courseHash, compatibilityId }, () => {
      this.requireMember(actor); this.requireMatch(matchId); this.requirePhase('Preparing');
      const match = this.match!;
      if (!validators.hash(courseHash) || !validators.hash(compatibilityId))
        throw new LobbyError('InvalidPreparation', 'course and compatibility identities must be SHA-256 values');
      if (courseHash !== match.course.expectedHash) {
        this.abortPreparationInternal();
        throw new LobbyError('CourseMismatch', 'local course does not match the catalog');
      }
      const existingIdentity = [...match.courseReady.values()][0]?.compatibilityId;
      if (existingIdentity && existingIdentity !== compatibilityId) {
        this.abortPreparationInternal();
        throw new LobbyError('CompatibilityMismatch', 'clients have different gameplay identities');
      }
      match.compatibilityId = compatibilityId;
      match.courseReady.set(actor, { courseHash, compatibilityId });
      return { allReady: match.courseReady.size === this.members.size, match: cloneMatch(match) };
    }, `match:${matchId}:member:${actor}`);
  }

  completePreparation(matchId: MatchId) {
    this.requireMatch(matchId); this.requirePhase('Preparing');
    if (this.match!.courseReady.size !== this.members.size) throw new LobbyError('PreparationIncomplete', 'both clients must load the course');
    this.lifecycle = 'Playing';
    return this.state();
  }

  abortPreparation(matchId: MatchId, _reason: string) {
    this.requireMatch(matchId); this.requirePhase('Preparing');
    this.abortPreparationInternal();
    return this.state();
  }

  assertCurrentMatch(matchId: MatchId) { this.requireMatch(matchId); }

  completeMatch(matchId: MatchId, scores: number[][]): MatchResult {
    this.requireMatch(matchId); this.requirePhase('Playing');
    const match = this.match!;
    this.validateScores(scores, match.roster.length);
    const totals = scores.map(row => row.reduce((sum, score) => sum + score, 0));
    const low = Math.min(...totals);
    this.result = {
      matchId, courseId: match.course.courseId, courseHash: match.course.expectedHash,
      roster: cloneRoster(match.roster), scores: scores.map(row => [...row]), par: [...(match.course.par ?? [])], totals,
      winnerPlayerIds: match.roster.filter((_, index) => totals[index] === low).map(player => player.playerId),
      status: 'Completed', completedAt: this.now(),
    };
    this.lifecycle = 'Results';
    return cloneResult(this.result);
  }

  interruptMatch(matchId: MatchId, reason: string, scores: number[][] = this.match?.roster.map(() => []) ?? []): MatchResult {
    this.requireMatch(matchId);
    if (this.lifecycle !== 'Preparing' && this.lifecycle !== 'Playing') throw new LobbyError('WrongPhase', 'match is not active');
    this.validateScores(scores, this.match!.roster.length);
    this.result = {
      matchId, courseId: this.match!.course.courseId, courseHash: this.match!.course.expectedHash,
      roster: cloneRoster(this.match!.roster), scores: scores.map(row => [...row]), par: [...(this.match!.course.par ?? [])],
      totals: scores.map(row => row.reduce((sum, score) => sum + score, 0)), winnerPlayerIds: [],
      status: 'Interrupted', reason: reason.slice(0, 160), completedAt: this.now(),
    };
    this.lifecycle = 'Results';
    return cloneResult(this.result);
  }

  returnToLobby(actor: MemberId, requestId: RequestId, matchId: MatchId) {
    return this.mutate(actor, requestId, 'ReturnToLobby', { matchId }, () => {
      this.requireMember(actor); this.requireMatch(matchId); this.requirePhase('Results');
      this.match!.returned.add(actor);
      if (this.match!.returned.size === this.members.size) {
        this.match = undefined;
        this.lifecycle = 'Open';
        this.changed();
      }
      return this.state();
    }, `match:${matchId}:member:${actor}`);
  }

  close() {
    this.lifecycle = 'Closed'; this.match = undefined; this.readiness.clear();
    return this.state();
  }

  private mutate<T>(actor: MemberId, requestId: RequestId, type: string, payload: unknown, action: () => T,
    scope = `lobby:${this.lobbyId}:member:${actor}`): T {
    this.requireMember(actor);
    return this.requests.run(scope, requestId, type, payload, action);
  }
  private requireMember(memberId: MemberId) {
    const member = this.members.get(memberId);
    if (!member) throw new LobbyError('NotMember', 'actor is not a lobby member');
    return member;
  }
  private requireOwner(memberId: MemberId) {
    this.requireMember(memberId);
    if (memberId !== this.ownerMemberId) throw new LobbyError('NotOwner', 'only the lobby owner may do that');
  }
  private requireRevision(revision: number) {
    if (revision !== this.revision) throw new LobbyError('StaleLobbyRevision', 'lobby state changed; refresh and try again');
  }
  private requirePhase(phase: LobbyPhase) {
    if (this.lifecycle !== phase) throw new LobbyError('WrongPhase', `lobby is ${this.lifecycle}, not ${phase}`);
  }
  private requireMatch(matchId: MatchId) {
    if (!this.match || this.match.matchId !== matchId) throw new LobbyError('StaleMatch', 'message does not belong to the current match');
  }
  private changed() {
    if (this.revision >= 1_000_000_000) throw new LobbyError('RevisionLimit', 'lobby revision limit reached');
    ++this.revision; this.readiness.clear();
  }
  private abortPreparationInternal() {
    this.match = undefined; this.lifecycle = 'Open'; this.changed();
  }
  private assertProfile(profile: MemberProfile) {
    if (!validators.displayName(profile.displayName) || !validators.color(profile.color))
      throw new LobbyError('InvalidProfile', 'display name or color is invalid');
  }
  private validateScores(scores: number[][], rosterSize: number) {
    if (!Array.isArray(scores) || scores.length !== rosterSize || scores.some(row => !Array.isArray(row)
      || row.length > 1000 || row.some(score => !Number.isSafeInteger(score) || score < 0 || score > 10_000)))
      throw new LobbyError('InvalidResult', 'score rows do not match the frozen roster');
  }
}
