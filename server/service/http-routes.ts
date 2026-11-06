// SPDX-License-Identifier: GPL-2.0-or-later
import type { IncomingMessage, ServerResponse } from 'node:http';
import { PROTOCOL_VERSION } from '../protocol/envelope.ts';
import type { ServerInfo } from './server-info.ts';

export interface HttpContext {
  info: ServerInfo;
  startedAt: number;
  now: () => number;
  stats: () => { lobbies: number; connections: number };
}

export const WEBSOCKET_PATHS = new Set(['/', '/ws']);
const JOIN_CODE = /^[A-HJ-NP-Z2-9]{6,12}$/;

const SECURITY_HEADERS = {
  'X-Content-Type-Options': 'nosniff',
  'Referrer-Policy': 'no-referrer',
  'X-Frame-Options': 'DENY',
};

export function normalizeJoinCode(raw: string): string | undefined {
  let decoded: string;
  try { decoded = decodeURIComponent(raw); } catch { return undefined; }
  const code = decoded.replace(/[\s-]/g, '').toUpperCase();
  return JOIN_CODE.test(code) ? code : undefined;
}

function escapeHtml(text: string): string {
  return text.replace(/[&<>"']/g, character => `&#${character.charCodeAt(0)};`);
}

function send(response: ServerResponse, status: number, type: string, body: string, head: boolean,
  extra: Record<string, string> = {}) {
  response.writeHead(status, { 'Content-Type': type, 'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store', ...SECURITY_HEADERS, ...extra });
  response.end(head ? undefined : body);
}

function websocketUrl(info: ServerInfo, request: IncomingMessage): string {
  if (info.publicUrl) {
    const url = new URL(info.publicUrl);
    return `${url.protocol === 'https:' ? 'wss' : 'ws'}://${url.host}/ws`;
  }
  return `ws://${request.headers.host ?? 'localhost'}/ws`;
}

export function joinPage(code: string, info: ServerInfo, request: IncomingMessage): string {
  const server = websocketUrl(info, request);
  const appLink = `kolf://join/${code}?server=${encodeURIComponent(server)}`;
  const downloads = info.latestClient?.url ?? (info.publicUrl ? `${info.publicUrl}/download` : undefined);
  const display = `${code.slice(0, 4)}-${code.slice(4)}`;
  return `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Join a Kolf game</title>
<style>
:root{color-scheme:light dark;--bg:#f6f7f4;--fg:#1d2321;--muted:#5b6661;--card:#fff;--accent:#1f7a52}
@media (prefers-color-scheme:dark){:root{--bg:#1d2023;--fg:#e8ece9;--muted:#a3ada8;--card:#272b2f;--accent:#4cc38a}}
body{margin:0;font:16px/1.5 system-ui,sans-serif;background:var(--bg);color:var(--fg);display:grid;place-items:center;min-height:100vh}
main{background:var(--card);padding:32px 24px;border-radius:16px;max-width:420px;margin:16px;text-align:center;box-shadow:0 2px 12px #0002}
h1{font-size:1.4rem;margin:0 0 8px}p{color:var(--muted);margin:8px 0}
.code{font:700 2.4rem/1.2 ui-monospace,monospace;letter-spacing:.12em;margin:20px 0}
a.button,button{display:inline-block;font:inherit;padding:10px 18px;border-radius:10px;border:0;margin:6px;cursor:pointer;text-decoration:none}
a.primary{background:var(--accent);color:#fff}button{background:transparent;color:var(--fg);border:1px solid var(--muted)}
</style></head><body><main>
<h1>Join a Kolf game</h1>
<p>Open Kolf, choose <strong>Play Online</strong>, and enter this code:</p>
<div class="code" id="code">${escapeHtml(display)}</div>
<a class="button primary" href="${escapeHtml(appLink)}">Open in Kolf</a>
<button type="button" id="copy">Copy code</button>
<p>Server: ${escapeHtml(info.serverName)}</p>
${downloads ? `<p><a href="${escapeHtml(downloads)}">Don't have Kolf? Download it</a></p>` : ''}
</main>
<script>
document.getElementById('copy').addEventListener('click',function(){
  var b=this;navigator.clipboard.writeText(${JSON.stringify(code)}).then(function(){b.textContent='Copied';});
});
</script></body></html>`;
}

export function handleHttpRequest(request: IncomingMessage, response: ServerResponse, context: HttpContext) {
  const method = request.method ?? 'GET';
  const head = method === 'HEAD';
  if (method !== 'GET' && !head) {
    send(response, 405, 'text/plain; charset=utf-8', 'method not allowed\n', false, { Allow: 'GET, HEAD' });
    return;
  }
  let path: string;
  try { path = new URL(request.url ?? '/', 'http://localhost').pathname; }
  catch { send(response, 400, 'text/plain; charset=utf-8', 'bad request\n', head); return; }

  if (path === '/healthz') { send(response, 200, 'text/plain; charset=utf-8', 'ok\n', head); return; }
  if (path === '/status') {
    const { lobbies, connections } = context.stats();
    const body = JSON.stringify({ name: context.info.serverName, version: context.info.serverVersion,
      protocolVersion: PROTOCOL_VERSION, lobbies, connections,
      uptime: Math.floor((context.now() - context.startedAt) / 1000) });
    send(response, 200, 'application/json; charset=utf-8', body, head);
    return;
  }
  const join = /^\/join\/([^/]+)\/?$/.exec(path);
  if (join) {
    const code = normalizeJoinCode(join[1]);
    if (code) {
      send(response, 200, 'text/html; charset=utf-8', joinPage(code, context.info, request), head, {
        'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; base-uri 'none'; form-action 'none'",
      });
      return;
    }
  }
  send(response, 404, 'text/plain; charset=utf-8', 'not found\n', head);
}
