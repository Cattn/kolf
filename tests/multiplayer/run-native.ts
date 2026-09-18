// SPDX-License-Identifier: GPL-2.0-or-later
// Windows integration runner: all native application launches go through Craft.
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const root = resolve(fileURLToPath(new URL('../..', import.meta.url)));
const fixture = process.argv[2] ?? 'static';
const action = process.argv[3] ?? 'rehit';
const fault = process.argv[4] ?? '';
const directory = resolve(root, 'server/prototype/local-session', `${fixture}-${action}-${Date.now()}`);
mkdirSync(directory, { recursive: true });
const course = resolve(root, `tests/multiplayer/fixtures/${fixture}.kolf`);
const relay = spawn(process.execPath, [resolve(root, 'server/prototype/relay.ts')], {
  cwd: root, env: { ...process.env, KOLF_COURSE: course, KOLF_SESSION_DIR: directory }, windowsHide: true,
});
const relayLog: string[] = [];
relay.stdout.on('data', data => { relayLog.push(data.toString()); });
relay.stderr.on('data', data => { relayLog.push(data.toString()); });
const pause = (ms: number) => new Promise(r => setTimeout(r, ms));
const clients: ReturnType<typeof spawn>[] = [];
try {
  for (let i = 0; !existsSync(resolve(directory, 'guest.json')); ++i) {
    if (i > 100 || relay.exitCode !== null) throw Error(`Relay failed: ${relayLog.join('')}`);
    await pause(100);
  }
  for (const role of ['authority', 'guest']) {
    const path = resolve(directory, `${role}.json`);
    const config = JSON.parse(readFileSync(path, 'utf8'));
    const turns = fixture === 'static' ? 4 : 40;
    config.scriptedShots = Array.from({ length: turns }, (_, i) => ({ directionRadians: 0, launchMagnitude: 1.8, advanced: i % 2 === 1 }));
    config.scriptedHazardAction = action; config.capture = true; config.exitWhenFinished = true;
    config.verifySnapshots = true;
    config.logFrames = true;
    if (role === 'guest' && fault) {
      config.fault = fault.startsWith('disconnect') ? 'disconnect' : 'resync';
      config.faultPhase = fault.includes('hazard') ? 'AwaitingHazardChoice' : 'Simulating';
      config.faultAfterMs = 300;
      if (fault.includes('hazard')) delete config.scriptedHazardAction;
    }
    writeFileSync(path, JSON.stringify(config));
    const p = spawn('C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe', ['-NoProfile', '-File',
      resolve(root, 'tests/multiplayer/launch.ps1'), '-Config', path], { cwd: root, windowsHide: true });
    p.stdout.on('data', () => {}); p.stderr.on('data', data => process.stderr.write(data)); clients.push(p);
  }
  for (let i = 0; ; ++i) {
    await pause(500);
    const results = ['authority', 'guest'].map(role => {
      const path = resolve(directory, role, 'session.jsonl');
      if (!existsSync(path)) return [];
      return readFileSync(path, 'utf8').trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
    });
    const interrupted = results.flat().find(e => e.event === 'interrupted');
    if (interrupted && !fault.startsWith('disconnect')) throw Error(JSON.stringify(interrupted));
    if (fault.startsWith('disconnect') && results.every(events => events.some(e => e.event === 'interrupted'))) {
      console.log(JSON.stringify({ fixture, fault, outcome: 'both interrupted', directory })); break;
    }
    if (results.every(events => events.some(e => e.event === 'applied' && e.state.phase === 'Finished'))) {
      const [host, guest] = results.map(events => events.filter(e => e.event === 'applied'));
      for (const e of guest) {
        assert.equal(e.physicsSteps, 0); assert.equal(e.collisions, 0);
        assert.equal(e.gameplayRandomCalls, 0);
        // Resync can capture newer positions without advancing the turn revision.
        const candidates = host.filter(h => h.revision === e.revision);
        assert(candidates.some(h => JSON.stringify(h.state) === JSON.stringify(e.state)), 'Guest commit matches an authority snapshot');
      }
      assert.equal(host.at(-1).state.phase, 'Finished');
      if (fixture === 'static') assert.deepEqual(host.at(-1).state.scores, [[1, 1], [1, 1]]);
      if (fixture === 'water') assert(host.some(e => e.state.phase === 'AwaitingHazardChoice'));
      const latencies = results.flat().filter(e => e.event === 'accepted').map(e => e.latencyMs).sort((a, b) => a - b);
      const frames = results[0].filter(e => e.event === 'frame');
      const received = results[1].filter(e => e.event === 'receivedFrame');
      for (const e of received) assert.deepEqual(e.state, frames.find(f => f.frameSeq === e.frameSeq)?.state);
      if (fixture === 'teleport') assert(frames.some(f => f.state.balls.some((b: any) => !b.visible && b.state === 1)));
      if (fault.startsWith('resync')) assert(results[1].some(e => e.event === 'requestResync'));
      const summary = { fixture, action, fault, transitions: host.length, receivedFrames: received.length, scores: host.at(-1).state.scores,
        guestPhysicsSteps: 0, guestCollisions: 0, medianAcceptanceMs: latencies[Math.floor(latencies.length / 2)],
        p95AcceptanceMs: latencies[Math.floor(latencies.length * .95)], directory };
      writeFileSync(resolve(directory, 'result.json'), JSON.stringify(summary, null, 2));
      console.log(JSON.stringify(summary)); await pause(1000); break;
    }
    if (i > 240) throw Error('Native match exceeded 120 seconds');
  }
} finally {
  writeFileSync(resolve(directory, 'relay.jsonl'), relayLog.join(''));
  relay.kill();
  for (const client of clients) {
    client.stdout?.destroy(); client.stderr?.destroy(); client.unref();
  }
  // The application exits on completion; retain a failed window for inspection.
}
