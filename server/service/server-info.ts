// SPDX-License-Identifier: GPL-2.0-or-later
import { readFileSync } from 'node:fs';
import type { JsonObject } from '../protocol/envelope.ts';

export interface LatestClient { version: string; url: string }
export interface ServerInfo {
  serverName: string;
  serverVersion: string;
  motd?: string;
  latestClient?: LatestClient;
  publicUrl?: string;
}

export const MAX_MOTD_LENGTH = 280;

const packageVersion = (() => {
  try {
    const parsed = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
    return typeof parsed.version === 'string' ? parsed.version : '0.0.0';
  } catch { return '0.0.0'; }
})();

const UNSAFE_RANGES: Array<[number, number]> = [[0x00, 0x1f], [0x7f, 0x9f], [0x200b, 0x200f], [0x2028, 0x202e], [0x2066, 0x2069]];
const UNSAFE_CHARACTERS = new RegExp(`[${UNSAFE_RANGES.map(([from, to]) =>
  `${String.fromCharCode(92)}u${from.toString(16).padStart(4, '0')}-${String.fromCharCode(92)}u${to.toString(16).padStart(4, '0')}`).join('')}]`, 'g');

export function plainText(value: string, maximumLength: number): string {
  return value.replace(UNSAFE_CHARACTERS, ' ').replace(/\s+/g, ' ').trim().slice(0, maximumLength);
}

function httpUrl(value: string | undefined): string | undefined {
  if (!value) return undefined;
  try {
    const url = new URL(value);
    return url.protocol === 'https:' || url.protocol === 'http:' ? url.toString().replace(/\/$/, '') : undefined;
  } catch { return undefined; }
}

export function serverInfoFromEnvironment(environment: NodeJS.ProcessEnv = process.env): ServerInfo {
  const info: ServerInfo = {
    serverName: plainText(environment.KOLF_SERVER_NAME ?? '', 60) || 'Kolf server',
    serverVersion: packageVersion,
  };
  const motd = plainText(environment.KOLF_MOTD ?? '', MAX_MOTD_LENGTH);
  if (motd) info.motd = motd;
  const publicUrl = httpUrl(environment.KOLF_PUBLIC_URL);
  if (publicUrl) info.publicUrl = publicUrl;
  const latestVersion = plainText(environment.KOLF_LATEST_CLIENT_VERSION ?? '', 40);
  const latestUrl = httpUrl(environment.KOLF_LATEST_CLIENT_URL);
  if (latestVersion && latestUrl) info.latestClient = { version: latestVersion, url: latestUrl };
  return info;
}

export function defaultServerInfo(): ServerInfo {
  return { serverName: 'Kolf server', serverVersion: packageVersion };
}

export function helloFields(info: ServerInfo): JsonObject {
  const fields: JsonObject = { serverName: info.serverName, serverVersion: info.serverVersion };
  if (info.motd) fields.motd = info.motd;
  if (info.latestClient) fields.latestClient = { version: info.latestClient.version, url: info.latestClient.url };
  return fields;
}
