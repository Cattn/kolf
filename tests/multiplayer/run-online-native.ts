// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded Windows online integration runner. Native clients are launched only through Craft.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:net';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../..', import.meta.url)));
const scenario = process.argv[2] ?? 'two-rematch';
assert(['two-rematch', 'four-player', 'four-player-hazard'].includes(scenario),
  'Scenario must be two-rematch, four-player, or four-player-hazard');
const variableRoster = scenario.startsWith('four-player');
const hazardScenario = scenario === 'four-player-hazard';
const clientCount = variableRoster ? 3 : 2;
const matchCount = scenario === 'two-rematch' ? 2 : 1;
const turnCount = hazardScenario ? 40 : variableRoster ? 8 : 4;
const directory = resolve(root, 'server/local-session', `online-${scenario}-${Date.now()}`);
const joinCodeFile = resolve(directory, 'join-code.txt');
const course = resolve(root, `tests/multiplayer/fixtures/${hazardScenario ? 'water' : 'static'}.kolf`);
mkdirSync(directory, { recursive: true });

const port = await new Promise<number>((resolvePort, reject) => {
  const reservation = createServer();
  reservation.once('error', reject);
  reservation.listen(0, '127.0.0.1', () => {
    const address = reservation.address();
    if (!address || typeof address === 'string') return reject(Error('Could not reserve a local port'));
    reservation.close(() => resolvePort(address.port));
  });
});
const relay = spawn(process.execPath, [resolve(root, 'server/main.ts')], {
  cwd: resolve(root, 'server'),
  env: { ...process.env, KOLF_BIND: '127.0.0.1', KOLF_PORT: String(port), KOLF_TEST_COURSE: course },
  windowsHide: true,
});
const relayLog: string[] = [];
relay.stdout.on('data', data => relayLog.push(data.toString()));
relay.stderr.on('data', data => relayLog.push(data.toString()));
const pause = (ms: number) => new Promise(resolvePause => setTimeout(resolvePause, ms));
const clients: ReturnType<typeof spawn>[] = [];
const clientLogs: string[][] = [];
// Craft's PowerShell environment bootstrap removes variables that are present in
// its clean environment snapshot. npm's transient variables can disappear while
// that list is applied, and $ErrorActionPreference=Stop then aborts the launcher.
const craftLauncherEnvironment = Object.fromEntries(Object.entries(process.env)
  .filter(([name]) => !name.toLowerCase().startsWith('npm_')));
const exited = (child: ReturnType<typeof spawn>) => child.exitCode !== null || child.signalCode !== null;
const stopLaunched = (child: ReturnType<typeof spawn>) => {
  if (exited(child)) return;
  if (process.platform === 'win32' && child.pid)
    spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
  else child.kill();
};
const sessionDirectories = (client: number) => {
  const clientDirectory = resolve(directory, `client-${client}`);
  if (!existsSync(clientDirectory)) return [];
  return readdirSync(clientDirectory, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && existsSync(resolve(clientDirectory, entry.name, 'session.jsonl')))
    .map(entry => resolve(clientDirectory, entry.name));
};
const events = (sessionDirectory: string) => readFileSync(resolve(sessionDirectory, 'session.jsonl'), 'utf8')
  .trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
const nativePids = () => Array.from(new Set(Array.from({ length: clientCount }, (_, client) =>
  sessionDirectories(client).flatMap(sessionDirectory => events(sessionDirectory)
    .filter(event => event.event === 'connect').map(event => event.processId))).flat()))
  .filter((pid): pid is number => Number.isInteger(pid) && pid > 0);
const isAlive = (pid: number) => {
  // On Windows a process stuck in DLL detach can make kill(pid, 0) report it
  // as exited even while tasklist still shows the process and its resources.
  if (process.platform === 'win32') {
    const listing = spawnSync('tasklist', ['/FI', `PID eq ${pid}`, '/FO', 'CSV', '/NH'],
      { windowsHide: true, encoding: 'utf8' });
    if (listing.status !== 0) throw Error(`Could not inspect native PID ${pid}: ${listing.stderr}`);
    return listing.stdout.split(/\r?\n/).some(line => line.startsWith('"') && line.split(',')[1] === `"${pid}"`);
  }
  try { process.kill(pid, 0); return true; } catch { return false; }
};

let succeeded = false;
try {
  for (let i = 0; !relayLog.join('').includes('"event":"listening"'); ++i) {
    if (i > 100 || exited(relay)) throw Error(`online service failed: ${relayLog.join('')}`);
    await pause(100);
  }
  for (let client = 0; client < clientCount; ++client) {
    const logDirectory = resolve(directory, `client-${client}`);
    const config = {
      endpoint: `ws://127.0.0.1:${port}`,
      role: client === 0 ? 'owner' : 'joiner',
      displayName: client === 0 ? 'Owner' : `Member ${client + 1}`,
      color: ['#3daee9ff', '#f67400ff', '#27ae60ff'][client],
      courseId: 'test', joinCodeFile, expectedMembers: clientCount,
      expectedPlayers: variableRoster ? 4 : 2, matches: matchCount, logDirectory,
      additionalPlayers: variableRoster && client === 0
        ? [{ displayName: 'Owner second ball', color: '#9b59b6ff' }] : [],
      scriptedHazardAction: hazardScenario ? 'rehit' : undefined,
      scriptedShots: Array.from({ length: turnCount }, (_, turn) => ({
        directionRadians: 0, launchMagnitude: 1.8, advanced: turn % 2 === 1,
      })),
    };
    mkdirSync(logDirectory, { recursive: true });
    const configPath = resolve(directory, `client-${client}.json`);
    writeFileSync(configPath, JSON.stringify(config, null, 2));
    const output: string[] = []; clientLogs.push(output);
    const child = spawn('C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe', ['-NoProfile', '-File',
      resolve(root, 'tests/multiplayer/launch-online-test.ps1'), '-Config', configPath, '-Course', course],
    { cwd: root, env: craftLauncherEnvironment, windowsHide: true });
    child.stdout.on('data', data => output.push(data.toString()));
    child.stderr.on('data', data => output.push(data.toString()));
    clients.push(child);
  }

  for (let i = 0; ; ++i) {
    await pause(500);
    const failed = clients.findIndex(client => exited(client) && client.exitCode !== 0);
    if (failed >= 0) throw Error(`client ${failed} failed (${clients[failed].exitCode}): ${clientLogs[failed].join('')}`);
    if (exited(relay)) throw Error(`online service exited early: ${relayLog.join('')}`);
    const interrupted = Array.from({ length: clientCount }, (_, client) => sessionDirectories(client))
      .flat().flatMap(sessionDirectory => events(sessionDirectory)).find(event => event.event === 'interrupted');
    if (interrupted) throw Error(`native session interrupted: ${interrupted.reason}`);
    const complete = Array.from({ length: clientCount }, (_, client) => sessionDirectories(client)).every(directories =>
      directories.length === matchCount && directories.every(sessionDirectory => {
        const log = events(sessionDirectory);
        return log.some(event => event.event === 'sceneDestroyed')
          && log.some(event => event.event === 'applied' && event.state?.phase === 'Finished');
      }));
    if (complete) break;
    if (i > 360) throw Error(`online native ${scenario} scenario exceeded 180 seconds`);
  }

  for (let client = 0; client < clientCount; ++client) {
    const directories = sessionDirectories(client);
    assert.equal(directories.length, matchCount, `client ${client} recorded every match`);
    for (const sessionDirectory of directories) {
      const log = events(sessionDirectory);
      assert(!log.some(event => event.event === 'interrupted'), `client ${client} was not interrupted`);
      const final = log.filter(event => event.event === 'applied').at(-1);
      assert.equal(final?.state?.phase, 'Finished');
      assert.equal(final.state.players?.length ?? final.state.balls?.length, variableRoster ? 4 : 2);
      if (client > 0) for (const applied of log.filter(event => event.event === 'applied')) {
        assert.equal(applied.physicsSteps, 0); assert.equal(applied.collisions, 0); assert.equal(applied.gameplayRandomCalls, 0);
      }
      assert(log.some(event => event.event === 'aimPreview'), `client ${client} saw the remote turn's aim`);
      assert(log.some(event => event.event === 'aimClear'), `client ${client} cleared the remote aim`);
    }
  }
  if (hazardScenario) assert(sessionDirectories(0).flatMap(events)
    .some(event => event.event === 'applied' && event.state?.phase === 'AwaitingHazardChoice'),
  'variable roster reached a hazard choice');
  const pids = nativePids();
  assert.equal(pids.length, clientCount, 'Every ordinary Kolf client recorded its process ID');
  for (let i = 0; i < 100 && pids.some(isAlive); ++i) await pause(100);
  assert(pids.every(pid => !isAlive(pid)), 'Every ordinary Kolf client exited cleanly');
  const summary = { scenario, clients: clientCount, players: variableRoster ? 4 : 2,
    matches: matchCount, nativeProcesses: pids.length, outcome: 'passed', directory };
  writeFileSync(resolve(directory, 'result.json'), JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary));
  succeeded = true;
} finally {
  writeFileSync(resolve(directory, 'relay.jsonl'), relayLog.join(''));
  for (let client = 0; client < clientLogs.length; ++client)
    writeFileSync(resolve(directory, `client-${client}-launcher.log`), clientLogs[client].join(''));
  for (const pid of nativePids()) if (isAlive(pid)) {
    if (process.platform === 'win32') spawnSync('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
    else process.kill(pid);
  }
  for (const client of clients) stopLaunched(client);
  stopLaunched(relay);
  if (!succeeded) process.exitCode = 1;
}
