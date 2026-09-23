// SPDX-License-Identifier: GPL-2.0-or-later
import type { ConnectionId, IdFactory, JoinCode, LobbyId, MatchId, MemberId, PlayerId, RequestId } from '../protocol/ids.ts';
import { validators } from '../protocol/codecs.ts';
import { LobbyError } from './errors.ts';
import { RequestCache } from './request-cache.ts';
import { CourseTransfer } from './course-transfer.ts';
import { resultStatistics, type PlayerStatistics } from './result-stats.ts';

export type LobbyPhase = 'Open' | 'Preparing' | 'Playing' | 'Results' | 'Closed';

export interface CourseCatalogEntry {
  courseId: string;
  displayName: string;
  expectedHash: string;
  resourceName?: string;
  par?: number[];
  source?: 'shipped' | 'uploaded';
  author?: string;
  holes?: number;
  totalPar?: number;
  sha256?: string;
  byteSize?: number;
}

export interface MemberProfile { displayName: string }
export interface PlayerProfile { displayName: string; colorMode: 'auto' | 'custom'; customColor?: string }
export interface LobbyMember extends MemberProfile {
  memberId: MemberId;
  connectionId: ConnectionId;
}
export interface LobbyPlayer extends PlayerProfile {
  resolvedColor: string;
  playerId: PlayerId;
  ownerMemberId: MemberId;
  order: number;
}
export interface FrozenPlayer extends PlayerProfile {
  resolvedColor: string;
  playerId: PlayerId;
  ownerMemberId: MemberId;
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
  courseName: string;
  courseHash: string;
  roster: FrozenPlayer[];
  scores: number[][];
  par: number[];
  totals: number[];
  winnerPlayerIds: PlayerId[];
  standings: PlayerStatistics[];
  skippedHoles: number[];
  durationMs: number;
  status: 'Completed' | 'Interrupted';
  reason?: string;
  completedAt: number;
}
export interface MatchMetrics {
  acceptedShots: number[];
  hazardChoices: number[];
  completedHoleCounts: number[];
  skippedHoles: number[];
  durationMs: number;
}
export interface LobbyState {
  lobbyId: LobbyId;
  joinCode: JoinCode;
  ownerMemberId: MemberId;
  lobbyRevision: number;
  phase: LobbyPhase;
  selectedCourseId: string;
  courses?: CourseCatalogEntry[];
  members: Array<LobbyMember & { ready: boolean; connected: true }>;
  players: LobbyPlayer[];
  rematchRequestedMemberIds: MemberId[];
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
  creatorPlayer: Omit<LobbyPlayer, 'resolvedColor'>;
  selectedCourseId: string;
  catalog: CourseCatalogEntry[];
  ids: IdFactory;
  maximumMembers: number;
  maximumPlayers: number;
  maximumRequests: number;
  now?: () => number;
}

const cloneCourse = (course: CourseCatalogEntry): CourseCatalogEntry => ({ ...course, par: course.par ? [...course.par] : undefined });
// Eight fixed, opaque colors chosen to stand out against green courses.
export const AUTO_COLORS = ['#0072b2ff', '#e69f00ff', '#cc79a7ff', '#d55e00ff',
  '#56b4e9ff', '#332288ff', '#aa3377ff', '#333333ff'] as const;
const rgb = (color: string) => [1, 3, 5].map(index => parseInt(color.slice(index, index + 2), 16));
const distance = (a: string, b: string) => {
  const left = rgb(a), right = rgb(b);
  return Math.sqrt(left.reduce((sum, channel, index) => sum + (channel - right[index]) ** 2, 0));
};
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
  skippedHoles: [...result.skippedHoles],
  par: [...result.par], totals: [...result.totals], winnerPlayerIds: [...result.winnerPlayerIds],
  standings: result.standings.map(row => ({ ...row,
    bestHole: row.bestHole && { ...row.bestHole }, worstHole: row.worstHole && { ...row.worstHole } })),
});

/** One isolated v3 lobby. Transport and authoritative simulation live elsewhere. */
export class LobbySession {
  readonly lobbyId: LobbyId;
  readonly joinCode: JoinCode;
  readonly ownerMemberId: MemberId;
  private readonly catalog = new Map<string, CourseCatalogEntry>();
  private readonly members = new Map<MemberId, LobbyMember>();
  private readonly players = new Map<PlayerId, LobbyPlayer>();
  private readonly readiness = new Map<MemberId, number>();
  private readonly rematchRequests = new Set<MemberId>();
  private readonly requests: RequestCache;
  private readonly ids: IdFactory;
  private readonly now: () => number;
  readonly transfers: CourseTransfer;
  private readonly maximumMembers: number;
  private readonly maximumPlayers: number;
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
    this.maximumMembers = options.maximumMembers;
    this.maximumPlayers = options.maximumPlayers;
    this.now = options.now ?? (() => Date.now());
    this.transfers = new CourseTransfer(this.now);
    this.requests = new RequestCache(this.now, options.maximumRequests);
    for (const course of options.catalog) {
      if (!validators.hash(course.expectedHash) || !validators.displayName(course.displayName)
        || !validators.identifier(course.courseId) || this.catalog.has(course.courseId))
        throw new LobbyError('InvalidCatalog', 'course catalog entry is invalid or duplicated');
      this.catalog.set(course.courseId, cloneCourse(course));
    }
    if (!this.catalog.has(options.selectedCourseId)) throw new LobbyError('UnknownCourse', 'selected course is not in the catalog');
    this.selectedCourseId = options.selectedCourseId;
    this.assertMemberProfile(options.creator);
    this.assertPlayerProfile(options.creatorPlayer);
    if (options.creatorPlayer.ownerMemberId !== options.creator.memberId || options.creatorPlayer.order !== 0)
      throw new LobbyError('InvalidPlayer', 'creator player ownership is invalid');
    this.members.set(options.creator.memberId, { ...options.creator });
    this.players.set(options.creatorPlayer.playerId, { ...options.creatorPlayer, resolvedColor: '' });
    this.resolveColors();
  }

  get phase() { return this.lifecycle; }
  get lobbyRevision() { return this.revision; }
  get currentMatchId() { return this.match?.matchId; }
  get memberCount() { return this.members.size; }
  get playerCount() { return this.players.size; }

  state(): LobbyState {
    return {
      lobbyId: this.lobbyId, joinCode: this.joinCode, ownerMemberId: this.ownerMemberId,
      lobbyRevision: this.revision, phase: this.lifecycle, selectedCourseId: this.selectedCourseId,
      courses: [...this.catalog.values()].map(cloneCourse),
      members: [...this.members.values()].map(member => ({ ...member, connected: true as const,
        ready: this.readiness.get(member.memberId) === this.revision })),
      players: this.orderedPlayers().map(player => ({ ...player })),
      rematchRequestedMemberIds: [...this.rematchRequests],
      match: this.match ? cloneMatch(this.match) : undefined,
      latestResult: this.result ? cloneResult(this.result) : undefined,
    };
  }

  member(memberId: MemberId): LobbyMember {
    const member = this.members.get(memberId);
    if (!member) throw new LobbyError('NotMember', 'connection is not a member of this lobby');
    return { ...member };
  }

  join(member: LobbyMember, defaultPlayer: Omit<LobbyPlayer, 'resolvedColor'>) {
    this.requirePhase('Open');
    if (this.members.size >= this.maximumMembers) throw new LobbyError('LobbyFull', 'this lobby has reached its member limit');
    if (this.players.size >= this.maximumPlayers) throw new LobbyError('LobbyFull', 'this lobby has reached its player limit');
    if ([...this.members.values()].some(existing => existing.connectionId === member.connectionId))
      throw new LobbyError('AlreadyJoined', 'connection is already in this lobby');
    this.assertMemberProfile(member); this.assertPlayerProfile(defaultPlayer);
    if (defaultPlayer.ownerMemberId !== member.memberId || defaultPlayer.order !== this.players.size)
      throw new LobbyError('InvalidPlayer', 'default player ownership is invalid');
    this.members.set(member.memberId, { ...member });
    this.players.set(defaultPlayer.playerId, { ...defaultPlayer, resolvedColor: '' });
    this.resolveColors();
    this.changed();
    return this.state();
  }

  updateMember(actor: MemberId, requestId: RequestId, patch: Partial<MemberProfile>) {
    return this.mutate(actor, requestId, 'UpdateMember', patch, () => {
      this.requirePhase('Open');
      const member = this.requireMember(actor);
      const displayName = patch.displayName ?? member.displayName;
      this.assertMemberProfile({ displayName });
      if (displayName !== member.displayName) { member.displayName = displayName; this.changed(false); }
      return this.state();
    });
  }

  addPlayer(actor: MemberId, requestId: RequestId, profile: PlayerProfile) {
    return this.mutate(actor, requestId, 'AddPlayer', profile, () => {
      this.requirePhase('Open'); this.assertPlayerProfile(profile);
      if (this.players.size >= this.maximumPlayers) throw new LobbyError('LobbyFull', 'this lobby has reached its player limit');
      const player: LobbyPlayer = { playerId: this.ids.player(), ownerMemberId: actor, order: this.players.size,
        ...profile, resolvedColor: '' };
      this.players.set(player.playerId, player); this.resolveColors(); this.changed();
      return { playerId: player.playerId, state: this.state() };
    });
  }

  updatePlayer(actor: MemberId, requestId: RequestId, playerId: PlayerId, patch: Partial<PlayerProfile>) {
    return this.mutate(actor, requestId, 'UpdatePlayer', { playerId, ...patch }, () => {
      this.requirePhase('Open');
      const player = this.requireOwnedPlayer(actor, playerId);
      const next = { displayName: patch.displayName ?? player.displayName,
        colorMode: patch.colorMode ?? player.colorMode,
        customColor: patch.colorMode === 'auto' ? undefined : patch.customColor ?? player.customColor };
      this.assertPlayerProfile(next);
      if (next.displayName !== player.displayName || next.colorMode !== player.colorMode
        || next.customColor?.toLowerCase() !== player.customColor?.toLowerCase()) {
        Object.assign(player, next);
        if (next.colorMode === 'auto') delete player.customColor;
        this.resolveColors(); this.changed();
      }
      return this.state();
    });
  }

  removePlayer(actor: MemberId, requestId: RequestId, playerId: PlayerId) {
    return this.mutate(actor, requestId, 'RemovePlayer', { playerId }, () => {
      this.requirePhase('Open'); this.requireOwnedPlayer(actor, playerId);
      if (this.ownedPlayers(actor).length <= 1) throw new LobbyError('LastPlayer', 'a member must keep at least one player');
      this.players.delete(playerId); this.normalizeOrder(); this.resolveColors(); this.changed();
      return this.state();
    });
  }

  reorderPlayers(actor: MemberId, requestId: RequestId, playerIds: PlayerId[]) {
    return this.mutate(actor, requestId, 'ReorderPlayers', { playerIds }, () => {
      this.requirePhase('Open');
      const owned = this.ownedPlayers(actor);
      if (playerIds.length !== owned.length || new Set(playerIds).size !== playerIds.length
        || playerIds.some(id => !owned.some(player => player.playerId === id)))
        throw new LobbyError('InvalidPlayerOrder', 'order must contain every locally owned player exactly once');
      const positions = owned.map(player => player.order).sort((a, b) => a - b);
      const changed = playerIds.some((id, index) => this.players.get(id)!.order !== positions[index]);
      if (changed) {
        playerIds.forEach((id, index) => { this.players.get(id)!.order = positions[index]; });
        this.normalizeOrder(); this.resolveColors(); this.changed();
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

  publishUploadedCourse(actor: MemberId, course: CourseCatalogEntry) {
    this.requireOwner(actor); this.requirePhase('Open');
    const existing = this.catalog.get(course.courseId);
    if (existing) {
      if (existing.expectedHash !== course.expectedHash || existing.source !== 'uploaded')
        throw new LobbyError('InvalidCourse', 'uploaded course ID conflicts with the catalog');
      if (this.selectedCourseId !== course.courseId) { this.selectedCourseId = course.courseId; this.changed(); }
      return this.state();
    }
    if (course.source !== 'uploaded' || !validators.hash(course.expectedHash)
      || !validators.identifier(course.courseId)
      || !course.displayName || [...course.displayName].length > 64)
      throw new LobbyError('InvalidCourse', 'uploaded course descriptor is invalid');
    this.catalog.set(course.courseId, cloneCourse(course));
    this.selectedCourseId = course.courseId;
    this.changed();
    return this.state();
  }

  selectedMatchCourse(matchId: MatchId) {
    this.requireMatch(matchId); this.requirePhase('Preparing');
    return cloneCourse(this.match!.course);
  }

  beginCourseUpload(actor: MemberId, uploadId: string, sha256: string, byteSize: number) {
    this.requireOwner(actor); this.requirePhase('Open');
    return this.transfers.begin(uploadId, sha256, byteSize);
  }

  appendCourseChunk(actor: MemberId, uploadId: string, index: number, data: string) {
    this.requireOwner(actor); this.requirePhase('Open');
    return this.transfers.chunk(uploadId, index, data);
  }

  finishCourseUpload(actor: MemberId, uploadId: string) {
    this.requireOwner(actor); this.requirePhase('Open');
    return this.publishUploadedCourse(actor, this.transfers.finish(uploadId));
  }

  downloadCourseChunk(actor: MemberId, matchId: MatchId, sha256: string, index: number) {
    this.requireMember(actor);
    const course = this.selectedMatchCourse(matchId);
    if (course.source !== 'uploaded' || course.expectedHash !== sha256)
      throw new LobbyError('CourseUnavailable', 'requested course is not selected for this match');
    return this.transfers.get(sha256, index);
  }

  setReady(actor: MemberId, requestId: RequestId, lobbyRevision: number, ready: boolean) {
    return this.mutate(actor, requestId, 'SetReady', { lobbyRevision, ready }, () => {
      this.requirePhase('Open'); this.requireRevision(lobbyRevision);
      if (ready) this.readiness.set(actor, this.revision); else this.readiness.delete(actor);
      return this.state();
    });
  }

  start(actor: MemberId, requestId: RequestId, lobbyRevision: number): MatchView {
    return this.mutate(actor, requestId, 'StartMatch', { lobbyRevision }, () => {
      this.requireOwner(actor); this.requireRevision(lobbyRevision);
      if (this.match && (this.lifecycle === 'Preparing' || this.lifecycle === 'Playing')) return cloneMatch(this.match);
      this.requirePhase('Open');
      if (this.members.size < 2) throw new LobbyError('LobbyNotReady', 'at least two connected members are required');
      if (this.players.size < 2 || this.players.size > this.maximumPlayers)
        throw new LobbyError('LobbyNotReady', `between 2 and ${this.maximumPlayers} players are required`);
      if ([...this.members].some(([memberId]) => this.readiness.get(memberId) !== this.revision))
        throw new LobbyError('LobbyNotReady', 'every member must be ready on the current revision');
      const course = this.catalog.get(this.selectedCourseId)!;
      const roster = this.orderedPlayers().map((player, engineIndex) => ({
        playerId: player.playerId, ownerMemberId: player.ownerMemberId, engineIndex,
        displayName: player.displayName, colorMode: player.colorMode, customColor: player.customColor,
        resolvedColor: player.resolvedColor,
      }));
      this.match = {
        matchId: this.ids.match(), authorityMemberId: this.ownerMemberId, course: cloneCourse(course), roster,
        courseReady: new Map(), returned: new Set(),
      };
      this.lifecycle = 'Preparing'; this.result = undefined; this.rematchRequests.clear();
      return cloneMatch(this.match);
    });
  }

  courseReady(actor: MemberId, requestId: RequestId, matchId: MatchId, courseHash: string, compatibilityId: string) {
    return this.mutate(actor, requestId, 'CourseReady', { matchId, courseHash, compatibilityId }, () => {
      this.requireMatch(matchId); this.requirePhase('Preparing');
      const match = this.match!;
      if (!validators.hash(courseHash) || !validators.hash(compatibilityId))
        throw new LobbyError('InvalidPreparation', 'course and compatibility identities must be SHA-256 values');
      if (courseHash !== match.course.expectedHash)
        throw new LobbyError('CourseMismatch', 'local course does not match the catalog');
      const existingIdentity = [...match.courseReady.values()][0]?.compatibilityId;
      if (existingIdentity && existingIdentity !== compatibilityId)
        throw new LobbyError('CompatibilityMismatch', 'clients have different gameplay identities');
      match.compatibilityId = compatibilityId;
      match.courseReady.set(actor, { courseHash, compatibilityId });
      return { allReady: match.courseReady.size === this.members.size, match: cloneMatch(match) };
    }, `match:${matchId}:member:${actor}`);
  }

  completePreparation(matchId: MatchId) {
    this.requireMatch(matchId); this.requirePhase('Preparing');
    if (this.match!.courseReady.size !== this.members.size) throw new LobbyError('PreparationIncomplete', 'every member must load the course');
    this.lifecycle = 'Playing'; return this.state();
  }

  abortPreparation(matchId: MatchId, _reason: string) {
    this.requireMatch(matchId); this.requirePhase('Preparing'); this.abortPreparationInternal(); return this.state();
  }

  assertCurrentMatch(matchId: MatchId) { this.requireMatch(matchId); }

  completeMatch(matchId: MatchId, scores: number[][], metrics?: MatchMetrics): MatchResult {
    this.requireMatch(matchId); this.requirePhase('Playing');
    const match = this.match!; this.validateScores(scores, match.roster.length);
    const totals = scores.map(row => row.reduce((sum, score) => sum + score, 0));
    const low = Math.min(...totals);
    const standings = resultStatistics(match.roster.map(player => player.playerId), scores, match.course.par ?? [],
      metrics?.acceptedShots, metrics?.hazardChoices, metrics?.completedHoleCounts, metrics?.skippedHoles);
    this.result = {
      matchId, courseId: match.course.courseId, courseName: match.course.displayName,
      courseHash: match.course.expectedHash,
      roster: cloneRoster(match.roster), scores: scores.map(row => [...row]), par: [...(match.course.par ?? [])], totals,
      winnerPlayerIds: match.roster.filter((_, index) => totals[index] === low).map(player => player.playerId),
      standings, skippedHoles: [...(metrics?.skippedHoles ?? [])], durationMs: metrics?.durationMs ?? 0,
      status: 'Completed', completedAt: this.now(),
    };
    this.lifecycle = 'Results'; return cloneResult(this.result);
  }

  interruptMatch(matchId: MatchId, reason: string, scores: number[][] = this.match?.roster.map(() => []) ?? [],
    metrics?: MatchMetrics): MatchResult {
    this.requireMatch(matchId);
    if (this.lifecycle !== 'Preparing' && this.lifecycle !== 'Playing') throw new LobbyError('WrongPhase', 'match is not active');
    this.validateScores(scores, this.match!.roster.length);
    this.result = {
      matchId, courseId: this.match!.course.courseId, courseName: this.match!.course.displayName,
      courseHash: this.match!.course.expectedHash,
      roster: cloneRoster(this.match!.roster), scores: scores.map(row => [...row]), par: [...(this.match!.course.par ?? [])],
      totals: scores.map(row => row.reduce((sum, score) => sum + score, 0)), winnerPlayerIds: [],
      standings: resultStatistics(this.match!.roster.map(player => player.playerId), scores, this.match!.course.par ?? [],
        metrics?.acceptedShots, metrics?.hazardChoices,
        metrics?.completedHoleCounts ?? scores.map(() => 0), metrics?.skippedHoles),
      skippedHoles: [...(metrics?.skippedHoles ?? [])], durationMs: metrics?.durationMs ?? 0,
      status: 'Interrupted', reason: reason.slice(0, 160), completedAt: this.now(),
    };
    this.lifecycle = 'Results'; return cloneResult(this.result);
  }

  returnToLobby(actor: MemberId, requestId: RequestId, matchId: MatchId, rematch = false) {
    return this.mutate(actor, requestId, 'ReturnToLobby', { matchId, rematch }, () => {
      this.requireMatch(matchId); this.requirePhase('Results'); this.match!.returned.add(actor);
      if (rematch) this.rematchRequests.add(actor);
      if (this.match!.returned.size === this.members.size) {
        this.match = undefined; this.lifecycle = 'Open'; this.changed();
      }
      return this.state();
    }, `match:${matchId}:member:${actor}`);
  }

  removeMember(memberId: MemberId) {
    this.requireMember(memberId);
    this.members.delete(memberId); this.readiness.delete(memberId); this.rematchRequests.delete(memberId);
    for (const player of this.ownedPlayers(memberId)) this.players.delete(player.playerId);
    this.normalizeOrder(); this.resolveColors();
    if (this.members.size === 0) return this.close();
    this.changed();
    if (this.lifecycle === 'Results' && this.match) {
      for (const returned of [...this.match.returned]) if (!this.members.has(returned)) this.match.returned.delete(returned);
      if (this.match.returned.size === this.members.size) { this.match = undefined; this.lifecycle = 'Open'; }
    }
    return this.state();
  }

  close() { this.lifecycle = 'Closed'; this.match = undefined; this.readiness.clear(); this.rematchRequests.clear(); return this.state(); }

  private mutate<T>(actor: MemberId, requestId: RequestId, type: string, payload: unknown, action: () => T,
    scope = `lobby:${this.lobbyId}:member:${actor}`): T {
    this.requireMember(actor); return this.requests.run(scope, requestId, type, payload, action);
  }
  private requireMember(memberId: MemberId) {
    const member = this.members.get(memberId);
    if (!member) throw new LobbyError('NotMember', 'actor is not a lobby member');
    return member;
  }
  private requireOwnedPlayer(memberId: MemberId, playerId: PlayerId) {
    const player = this.players.get(playerId);
    if (!player) throw new LobbyError('PlayerNotFound', 'player is not in this lobby');
    if (player.ownerMemberId !== memberId) throw new LobbyError('NotPlayerOwner', 'player belongs to another member');
    return player;
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
  private changed(clearReadiness = true) {
    if (this.revision >= 1_000_000_000) throw new LobbyError('RevisionLimit', 'lobby revision limit reached');
    ++this.revision;
    if (clearReadiness) this.readiness.clear();
    else for (const memberId of this.readiness.keys()) this.readiness.set(memberId, this.revision);
  }
  private abortPreparationInternal() { this.match = undefined; this.lifecycle = 'Open'; this.changed(); }
  private ownedPlayers(memberId: MemberId) { return this.orderedPlayers().filter(player => player.ownerMemberId === memberId); }
  private orderedPlayers() { return [...this.players.values()].sort((a, b) => a.order - b.order || a.playerId.localeCompare(b.playerId)); }
  private normalizeOrder() { this.orderedPlayers().forEach((player, order) => { player.order = order; }); }
  private resolveColors() {
    const roster = this.orderedPlayers();
    const used = roster.filter(player => player.colorMode === 'custom').map(player => player.customColor!.toLowerCase());
    for (const player of roster) {
      if (player.colorMode === 'custom') {
        player.resolvedColor = player.customColor!.toLowerCase();
        continue;
      }
      const unused = AUTO_COLORS.filter(candidate => !used.includes(candidate));
      const choices = unused.length ? unused : [...AUTO_COLORS];
      const best = choices.map(candidate => ({ candidate,
        separation: used.length ? Math.min(...used.map(other => distance(candidate, other))) : Infinity }))
        .sort((a, b) => b.separation - a.separation || AUTO_COLORS.indexOf(a.candidate) - AUTO_COLORS.indexOf(b.candidate))[0];
      player.resolvedColor = best.candidate;
      used.push(best.candidate);
    }
  }
  private assertMemberProfile(profile: MemberProfile) {
    if (!validators.displayName(profile.displayName)) throw new LobbyError('InvalidProfile', 'member display name is invalid');
  }
  private assertPlayerProfile(profile: PlayerProfile) {
    if (!validators.displayName(profile.displayName)
      || (profile.colorMode !== 'auto' && profile.colorMode !== 'custom')
      || (profile.colorMode === 'auto' && profile.customColor !== undefined)
      || (profile.colorMode === 'custom' && !validators.color(profile.customColor)))
      throw new LobbyError('InvalidPlayer', 'player display name or color is invalid');
  }
  private validateScores(scores: number[][], rosterSize: number) {
    if (!Array.isArray(scores) || scores.length !== rosterSize || scores.some(row => !Array.isArray(row)
      || row.length > 1000 || row.some(score => !Number.isSafeInteger(score) || score < 0 || score > 10_000)))
      throw new LobbyError('InvalidResult', 'score rows do not match the frozen roster');
  }
}
