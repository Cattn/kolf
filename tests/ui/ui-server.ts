// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the Kolf server on 127.0.0.1:13111 for one UI script, configured by the script's
// "server" block: { "env": { "KOLF_MOTD": "..." }, "drainAfterSeconds": 8 }.
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const script = process.argv[2];
if (!script) throw Error('usage: node ui-server.ts <ui-script.json>');
const config = JSON.parse(readFileSync(script, 'utf8')).server ?? {};
const serverDirectory = resolve(dirname(fileURLToPath(import.meta.url)), '../../server');

process.env.KOLF_BIND = '127.0.0.1';
process.env.KOLF_PORT = '13111';
for (const [key, value] of Object.entries(config.env ?? {})) process.env[key] = String(value);
process.chdir(serverDirectory);
await import(pathToFileURL(resolve(serverDirectory, 'main.ts')).href);

const drainAfter = Number(config.drainAfterSeconds ?? 0);
if (drainAfter > 0) setTimeout(() => process.emit('SIGTERM'), drainAfter * 1000);
