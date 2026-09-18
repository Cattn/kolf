// SPDX-License-Identifier: GPL-2.0-or-later
// Windows integration runner: all native application launches go through Craft.
import { spawn, spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:net';
import assert from 'node:assert/strict';

const root = resolve(fileURLToPath(new URL('../..', import.meta.url)));
const fixture = process.argv[2] ?? 'static';
const action = process.argv[3] ?? 'rehit';
const fault = process.argv[4] ?? '';
const directory = resolve(root, 'server/prototype/local-session', `${fixture}-${action}-${Date.now()}`);
mkdirSync(directory, { recursive: true });
const course = resolve(root, `tests/multiplayer/fixtures/${fixture}.kolf`);
const port = process.env.KOLF_PORT ?? await new Promise<number>((resolvePort, reject) => {
  const reservation = createServer();
  reservation.once('error', reject);
  reservation.listen(0, '127.0.0.1', () => {
    const address = reservation.address();
    if (!address || typeof address === 'string') return reject(Error('Could not reserve a local port'));
    reservation.close(() => resolvePort(address.port));
  });
});
const relay = spawn(process.execPath, [resolve(root, 'server/prototype/relay.ts')], {
  cwd: root, env: { ...process.env, KOLF_COURSE: course, KOLF_SESSION_DIR: directory, KOLF_PORT: String(port) }, windowsHide: true,
});
const relayLog: string[] = [];
relay.stdout.on('data', data => { relayLog.push(data.toString()); });
relay.stderr.on('data', data => { relayLog.push(data.toString()); });
const pause = (ms: number) => new Promise(r => setTimeout(r, ms));
const clients: ReturnType<typeof spawn>[] = [];
const exited = (child: ReturnType<typeof spawn>) => child.exitCode !== null || child.signalCode !== null;
const waitForExit = async (child: ReturnType<typeof spawn>, timeoutMs: number) => {
  if (exited(child)) return child.exitCode;
  return await new Promise<number | null>((resolveExit, reject) => {
    const timer = setTimeout(() => reject(Error(`Started process ${child.pid} did not exit within ${timeoutMs} ms`)), timeoutMs);
    child.once('exit', code => { clearTimeout(timer); resolveExit(code); });
  });
};
const stopLaunched = async (child: ReturnType<typeof spawn>) => {
  if (exited(child)) return;
  if (process.platform === 'win32' && child.pid) {
    // The PowerShell launcher owns Craft and its Kolf child; stop this tree only.
    spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
  } else child.kill();
  await waitForExit(child, 5000).catch(() => {});
};
const nativeEvents = (role: string) => {
  const path = resolve(directory, role, 'session.jsonl');
  return existsSync(path) ? readFileSync(path, 'utf8').trim().split('\n').filter(Boolean).map(line => JSON.parse(line)) : [];
};
const nativePids = () => ['authority', 'guest'].map(role =>
  nativeEvents(role).find(event => event.event === 'connect')?.processId).filter((pid): pid is number => Number.isInteger(pid) && pid > 0);
const isAlive = (pid: number) => {
  try { process.kill(pid, 0); return true; } catch { return false; }
};
let completed = false;
try {
  for (let i = 0; !existsSync(resolve(directory, 'guest.json')) || !relayLog.join('').includes('"event":"listening"'); ++i) {
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
    const failedClient = clients.findIndex((client, index) =>
      (exited(client) && client.exitCode !== 0)
      || (exited(client) && !nativeEvents(index === 0 ? 'authority' : 'guest').some(e => e.event === 'connect')));
    if (failedClient >= 0) {
      const role = failedClient === 0 ? 'authority' : 'guest';
      throw Error(`${role} Craft launcher exited before the match (code ${clients[failedClient].exitCode})`);
    }
    if (exited(relay)) throw Error(`Relay exited before the match: ${relayLog.join('')}`);
    const results = ['authority', 'guest'].map(nativeEvents);
    const interrupted = results.flat().find(e => e.event === 'interrupted');
    if (interrupted && !fault.startsWith('disconnect')) throw Error(JSON.stringify(interrupted));
    if (fault.startsWith('disconnect') && results.every(events => events.some(e => e.event === 'interrupted'))) {
      console.log(JSON.stringify({ fixture, fault, outcome: 'both interrupted', directory })); completed = true; break;
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
      if (fixture.startsWith('water')) assert(host.some(e => e.state.phase === 'AwaitingHazardChoice'));
      if (fixture === 'water-slope' && action === 'drop') {
        const hazardIndex = host.findIndex(e => e.state.phase === 'AwaitingHazardChoice');
        assert.equal(host[hazardIndex + 1]?.state.phase, 'Simulating',
          'Drop onto slope resumes simulation before advancing the turn');
        assert.deepEqual(host[hazardIndex + 1].state.scores, host[hazardIndex].state.scores,
          'Placement does not score the original stroke twice');
      }
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
      console.log(JSON.stringify(summary)); completed = true; break;
    }
    if (i > 240) throw Error('Native match exceeded 120 seconds');
  }
  if (completed) {
    const pids = nativePids();
    assert.equal(pids.length, 2, 'Both native process IDs were recorded');
    for (let i = 0; i < 100 && pids.some(isAlive); ++i) await pause(100);
    assert(pids.every(pid => !isAlive(pid)), 'Both native clients exited');
    for (const role of ['authority', 'guest']) {
      assert(nativeEvents(role).some(event => event.event === 'sceneDestroyed'), `${role} destroyed its game scene`);
    }
  }
} finally {
  writeFileSync(resolve(directory, 'relay.jsonl'), relayLog.join(''));
  // Craft --run can keep its PowerShell/Python launcher alive after Kolf exits.
  // Its native process IDs come from this invocation's own logs.
  for (const pid of nativePids()) if (isAlive(pid)) {
    if (process.platform === 'win32') spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
    else process.kill(pid);
  }
  for (const client of clients) await stopLaunched(client);
  await stopLaunched(relay);
}
