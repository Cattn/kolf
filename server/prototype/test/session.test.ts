// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Session } from '../session.ts';
import type { Peer } from '../session.ts';
import { decode, envelope, MAX_BYTES, validShot } from '../protocol.ts';
import type { Message, Role } from '../protocol.ts';
import { readFileSync } from 'node:fs';
import { OrderedQueue } from '../orderedqueue.ts';

test('delayed frames coalesce while reliable transitions keep their order', () => {
  const q = new OrderedQueue(100), received: string[] = [];
  q.push('frame-1', true, 100); q.push('frame-2', true, 101);
  q.push('settled', false, 50); q.push('frame-3', true, 60);
  q.drain(99, text => received.push(text)); assert.equal(received.length, 0);
  q.drain(101, text => received.push(text)); assert.deepEqual(received, ['frame-2', 'settled', 'frame-3']);
  assert.equal(q.bytes, 0); assert.equal(q.coalesced, 1);
  assert(!q.push('x'.repeat(101), false, 0));
});
test('a persistently slow consumer has bounded retained bytes', () => {
  const q = new OrderedQueue(100);
  for (let i = 0; i < 10000; ++i) assert(q.push('x'.repeat(80), true, i));
  assert.equal(q.bytes, 80); assert.equal(q.coalesced, 9999);
  assert(!q.push('control'.repeat(10), false, 10000));
});

test('shared C++/TypeScript golden shot cases', () => {
  const fixture = JSON.parse(readFileSync(new URL('../../../protocol/shot-fixtures.json', import.meta.url), 'utf8'));
  for (const c of fixture.cases) assert.equal(validShot({ ...fixture.base, ...c.patch }), c.valid, c.name);
});

function setup() {
  let time = 0;
  const session = new Session({ authority: 'a', guest: 'b' }, () => time);
  const messages: Record<Role, Message[]> = { authority: [], guest: [] };
  const peer = (role: Role): Peer => ({ role, send: m => messages[role].push(m), close() {} });
  const authority = peer('authority'), guest = peer('guest');
  const hello = (role: Role) => envelope('Hello', { role, credential: role === 'authority' ? 'a' : 'b', buildId: 'build', courseHash: 'a'.repeat(64) });
  session.join(authority, hello('authority')); session.join(guest, hello('guest'));
  const send = (p: Peer, type: string, fields: Message = {}) => session.receive(p, JSON.stringify(envelope(type, fields)));
  send(authority, 'CourseReady', { manifestHash: 'b'.repeat(64) }); send(guest, 'CourseReady', { manifestHash: 'b'.repeat(64) });
  const state = (fields: Message = {}) => ({ stateRevision: 1, holeGeneration: 1, turnId: 1, activeSlot: 0, hole: 1, par: 3,
    phase: 'AwaitingShot', manifestHash: 'b'.repeat(64), courseHash: 'a'.repeat(64),
    balls: [0, 1].map(i => ({id: `ball/${i}`, kind: 'sprite', x: 100, y: 200, z: 1, visible: true, rotation: 0, opacity: 1, sprite: 'ball', frame: -1, state: 1})),
    objects: [], scores: [[0], [0]], ...fields });
  const apply = (revision = 1) => {
    send(authority, 'StateApplied', { stateRevision: revision, syncId: session.syncId, manifestHash: 'b'.repeat(64) });
    send(guest, 'StateApplied', { stateRevision: revision, syncId: session.syncId, manifestHash: 'b'.repeat(64) });
  };
  send(authority, 'InitialState', { state: state() }); apply();
  const shot = (fields: Message = {}) => ({ commandId: 'shot-1', holeGeneration: 1, turnId: 1, playerSlot: 0,
    puttingMode: 'normal', directionRadians: 0, launchMagnitude: 2, ...fields });
  return { session, messages, authority, guest, send, shot, state, apply, advance: (ms: number) => { time += ms; session.tick(); }, hello };
}
test('one admitted shot per turn including identical retries and conflicting IDs', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'SubmitShot', h.shot({ commandId: 'shot-2' }));
  h.send(h.authority, 'SubmitShot', h.shot({ launchMagnitude: 3 }));
  assert.equal(h.messages.authority.filter(m => m.type === 'AdmitShot').length, 1);
  assert.equal(h.messages.authority.filter(m => m.type === 'CommandRejected').length, 2);
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'SubmitShot', h.shot());
  assert.equal(h.messages.authority.at(-1)?.type, 'ShotAccepted');
});
test('wrong owner, stale turn, stale generation and nonfinite numbers never enter authority', () => {
  const h = setup();
  h.send(h.guest, 'SubmitShot', h.shot());
  h.send(h.authority, 'SubmitShot', h.shot({ turnId: 2 }));
  h.send(h.authority, 'SubmitShot', h.shot({ holeGeneration: 2 }));
  for (const n of [NaN, Infinity, -1, 0, 7]) h.send(h.authority, 'SubmitShot', h.shot({ launchMagnitude: n }));
  assert.equal(h.messages.authority.filter(m => m.type === 'AdmitShot').length, 0);
});
test('authority impersonation and role takeover fail', () => {
  const h = setup();
  assert.throws(() => h.send(h.guest, 'CommitTransition', { state: h.state({ stateRevision: 2 }) }));
  assert.throws(() => h.session.join(h.authority, h.hello('authority')));
});
test('next turn opens only after both clients apply the committed transition', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 2, phase: 'Simulating' }) }); h.apply(2);
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3, turnId: 2, activeSlot: 1, scores: [[1], [0]] }) });
  h.send(h.guest, 'SubmitShot', h.shot({ commandId: 'shot-2', turnId: 2, playerSlot: 1 }));
  assert.equal(h.messages.guest.at(-1)?.type, 'CommandRejected');
  h.apply(3);
  h.send(h.guest, 'SubmitShot', h.shot({ commandId: 'shot-2', turnId: 2, playerSlot: 1 }));
  assert.equal(h.messages.authority.at(-1)?.type, 'AdmitShot');
});
test('duplicate commit is idempotent, conflicting or skipped revisions fail', () => {
  const h = setup(), seq = h.session.eventSeq;
  h.send(h.authority, 'InitialState', { state: h.state() });
  assert.equal(h.session.eventSeq, seq);
  assert.throws(() => h.send(h.authority, 'CommitTransition', { state: h.state({ par: 4 }) }));
  assert.throws(() => h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3 }) }));
});
test('remote hazard retry admits one choice and rejects the wrong owner', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 2, phase: 'Simulating' }) }); h.apply(2);
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3, phase: 'AwaitingHazardChoice', choiceId: 'water-1', choiceSlot: 1 }) }); h.apply(3);
  const choice = { choiceId: 'water-1', stateRevision: 3, syncId: h.session.syncId, action: 'rehit' };
  h.send(h.authority, 'ChooseHazardAction', choice);
  h.send(h.guest, 'ChooseHazardAction', choice); h.send(h.guest, 'ChooseHazardAction', choice);
  h.send(h.guest, 'ChooseHazardAction', { ...choice, stateRevision: 1 });
  assert.equal(h.messages.authority.filter(m => m.type === 'AdmitHazardAction').length, 1);
});
test('resync does not re-admit shots and disconnect is terminal', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.guest, 'RequestResync');
  h.send(h.authority, 'FullState', { state: h.state(), syncId: h.session.syncId }); h.apply();
  h.send(h.authority, 'SubmitShot', h.shot());
  assert.equal(h.messages.authority.filter(m => m.type === 'AdmitShot').length, 1);
  h.session.disconnect(h.guest);
  assert(h.session.interrupted);
  assert.throws(() => h.session.join(h.guest, h.hello('guest')));
});
test('a same-revision resync needs a fresh acknowledgement from both peers', () => {
  const h = setup();
  h.send(h.guest, 'RequestResync');
  assert(h.session.barrier);
  // These acknowledgements belong to the original commit, not the new resync.
  h.send(h.authority, 'StateApplied', { stateRevision: 1, syncId: 1, manifestHash: 'b'.repeat(64) });
  h.send(h.guest, 'StateApplied', { stateRevision: 1, syncId: 1, manifestHash: 'b'.repeat(64) });
  assert(h.session.barrier, 'Old acknowledgements must not reopen input before FullState');
  h.send(h.authority, 'FullState', { state: h.state(), syncId: h.session.syncId });
  h.apply();
  assert.equal(h.session.barrier, false);
});
test('close resync requests share one round and retries are rate limited', () => {
  const h = setup();
  h.send(h.guest, 'RequestResync');
  const round = h.session.syncId;
  h.send(h.guest, 'RequestResync');
  h.send(h.authority, 'RequestResync');
  assert.equal(h.session.syncId, round);
  assert.equal(h.messages.authority.filter(m => m.type === 'RequestResync').length, 1);
  h.send(h.authority, 'FullState', { state: h.state(), syncId: round });
  h.send(h.authority, 'StateApplied', { stateRevision: 1, syncId: round, manifestHash: 'b'.repeat(64) });
  h.send(h.guest, 'StateApplied', { stateRevision: 1, syncId: round - 1, manifestHash: 'b'.repeat(64) });
  assert(h.session.barrier, 'The old guest acknowledgement cannot complete the new round');
  h.send(h.guest, 'StateApplied', { stateRevision: 1, syncId: round, manifestHash: 'b'.repeat(64) });
  assert.equal(h.session.barrier, false);
  h.send(h.guest, 'RequestResync');
  assert.equal(h.session.syncId, round, 'Immediate retry waits for the cooldown');
  assert(h.session.barrier, 'The requester stays blocked until the queued round starts');
  h.send(h.authority, 'FullState', { state: h.state(), syncId: round });
  assert.equal(h.messages.guest.filter(m => m.type === 'FullState').length, 1,
    'A late full state from the previous round cannot satisfy the queued request');
  h.advance(1000);
  assert.equal(h.messages.authority.filter(m => m.type === 'RequestResync').length, 2);
  h.send(h.guest, 'RequestResync');
  assert.equal(h.session.syncId, round + 1, 'The queued request starts one fresh round');
});
test('a new commit cancels a queued resync and opens only its own round', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 2, phase: 'Simulating' }) });
  h.apply(2);
  h.send(h.guest, 'RequestResync');
  h.send(h.authority, 'FullState', { state: h.state({ stateRevision: 2, phase: 'Simulating' }), syncId: h.session.syncId });
  h.apply(2);
  h.send(h.guest, 'RequestResync');
  const requestCount = h.messages.authority.filter(m => m.type === 'RequestResync').length;
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3, turnId: 2, activeSlot: 1, scores: [[1], [0]] }) });
  h.advance(1000);
  assert.equal(h.messages.authority.filter(m => m.type === 'RequestResync').length, requestCount);
  h.apply(3);
  assert.equal(h.session.barrier, false);
});
test('a commit supersedes a pending resync without accepting its old full state', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 2, phase: 'Simulating' }) });
  h.apply(2);
  h.send(h.guest, 'RequestResync');
  const oldRound = h.session.syncId;
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3, turnId: 2, activeSlot: 1, scores: [[1], [0]] }) });
  const newRound = h.session.syncId;
  assert.equal(newRound, oldRound + 1);
  h.send(h.authority, 'FullState', { state: h.state({ stateRevision: 2, phase: 'Simulating' }), syncId: oldRound });
  h.send(h.guest, 'StateApplied', { stateRevision: 2, syncId: oldRound, manifestHash: 'b'.repeat(64) });
  assert(h.session.barrier);
  assert.equal(h.messages.guest.filter(m => m.type === 'InputReady' && m.syncId === newRound).length, 0);
  h.apply(3);
  assert.equal(h.session.barrier, false);
  assert.equal(h.session.state?.turnId, 2);
});
test('rapid completion supersedes an older state barrier', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 2, phase: 'Simulating' }) });
  h.send(h.authority, 'StateApplied', { stateRevision: 2, syncId: 2, manifestHash: 'b'.repeat(64) });
  h.send(h.authority, 'CommitTransition', { state: h.state({ stateRevision: 3, turnId: 2, activeSlot: 1, scores: [[1], [0]] }) });
  h.send(h.guest, 'StateApplied', { stateRevision: 2, syncId: 2, manifestHash: 'b'.repeat(64) });
  assert(h.session.barrier);
  assert.equal(h.messages.guest.filter(m => m.type === 'InputReady' && m.stateRevision === 3).length, 0);
  h.apply(3);
  assert.equal(h.session.barrier, false);
});
test('an old visual frame cannot cross a hole transition', () => {
  const h = setup();
  h.send(h.authority, 'SubmitShot', h.shot());
  h.send(h.authority, 'ShotAccepted', { commandId: 'shot-1' });
  const moving = h.state({ stateRevision: 2, phase: 'Simulating' });
  h.send(h.authority, 'CommitTransition', { state: moving }); h.apply(2);
  const nextHole = h.state({ stateRevision: 3, holeGeneration: 2, hole: 2, turnId: 2, scores: [[1, 0], [0, 0]] });
  h.send(h.authority, 'CommitTransition', { state: nextHole });
  const before = h.messages.guest.filter(m => m.type === 'StateFrame').length;
  h.send(h.authority, 'StateFrame', { state: moving, frameSeq: 1, syncId: 2 });
  assert.equal(h.messages.guest.filter(m => m.type === 'StateFrame').length, before);
  h.apply(3);
  assert.equal(h.session.state?.hole, 2);
});
test('malformed envelopes and bounded messages', () => {
  for (const raw of ['null', '[]', '{}', '{', JSON.stringify(envelope('Hello', { v: 2 })).replace('"v":1', '"v":2'), ' '.repeat(MAX_BYTES + 1)]) assert.throws(() => decode(raw));
  assert(!validShot({}));
});
test('missing acceptance times out instead of reopening ownership', () => {
  const h = setup(); h.send(h.authority, 'SubmitShot', h.shot()); h.advance(300001);
  assert(h.session.interrupted); assert.equal(h.session.pending, 'shot-1');
});
