// SPDX-License-Identifier: GPL-2.0-or-later
import { basename, dirname, resolve } from 'node:path';
import { loadCourseCatalog } from './service/course-catalog.ts';
import { LobbyWebSocketService } from './service/websocket-service.ts';
import { serverInfoFromEnvironment } from './service/server-info.ts';

const host = process.env.KOLF_BIND ?? '0.0.0.0';
const port = Number(process.env.KOLF_PORT ?? 3011);
if (!Number.isInteger(port) || port < 0 || port > 65535) throw Error('KOLF_PORT must be a valid port');
const courseRoot = resolve(process.env.KOLF_COURSE_ROOT ?? '../courses');
const testCourse = process.env.KOLF_TEST_COURSE;
const limit = (name: string) => {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return undefined;
  const value = Number(raw);
  if (!Number.isInteger(value) || value < 1 || value > 100000) throw Error(`${name} must be a positive integer`);
  return value;
};
const limits = Object.fromEntries(Object.entries({
  maximumLobbies: limit('KOLF_MAX_LOBBIES'), maximumConnections: limit('KOLF_MAX_CONNECTIONS'),
}).filter(([, value]) => value !== undefined));
const info = serverInfoFromEnvironment();
const catalog = testCourse
  ? loadCourseCatalog(dirname(resolve(testCourse)), [{ courseId: 'test', displayName: 'Automation fixture', fileName: basename(testCourse) }])
  : loadCourseCatalog(courseRoot);
const service = new LobbyWebSocketService({ host, port, catalog, limits, info });
await service.ready();
console.log(JSON.stringify({ event: 'listening', endpoint: service.endpoint(), protocolVersion: 3,
  serverName: info.serverName, serverVersion: info.serverVersion,
  courses: catalog.map(course => ({ courseId: course.courseId, displayName: course.displayName,
    resourceName: course.resourceName, expectedHash: course.expectedHash })) }));

let stopping = false;
const stop = async () => {
  if (stopping) return;
  stopping = true;
  await service.close();
};
process.on('SIGINT', stop);
process.on('SIGTERM', stop);
