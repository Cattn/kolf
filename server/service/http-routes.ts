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
<title>Join a game of Kolf</title>
<link rel="icon" href="/kolf.svg" type="image/svg+xml">
<style>
:root{color-scheme:light dark;--paper:#fff;--ink:#1d211f;--quiet:#5c6560;--rule:#e1e4e1;--green:#2c7a3a;--on-green:#fff}
@media (prefers-color-scheme:dark){:root{--paper:#1f1f1f;--ink:#ececec;--quiet:#a8aeab;--rule:#383838;--green:#6cbf78;--on-green:#14301a}}
body{margin:0;background:var(--paper);color:var(--ink);font:1.0625rem/1.6 system-ui,-apple-system,"Segoe UI","Noto Sans",Ubuntu,sans-serif}
main{max-width:34rem;margin:0 auto;padding:4.5rem 1rem 2.5rem}
.home{display:inline-flex;align-items:center;gap:.5rem;font-weight:700;color:var(--ink);text-decoration:none;margin-bottom:3rem}
h1{font-size:2rem;line-height:1.15;letter-spacing:-.02em;margin:0 0 .75rem}
p{margin:0 0 1rem}.quiet{color:var(--quiet)}
.code{font-size:4rem;font-weight:700;line-height:1;letter-spacing:.02em;font-variant-numeric:tabular-nums;margin:2rem 0 2.25rem;user-select:all}
.actions{display:flex;flex-wrap:wrap;gap:.75rem;margin-bottom:2.5rem}
.button{display:inline-block;background:var(--green);color:var(--on-green);font:inherit;font-weight:600;padding:.6rem 1.15rem;border:0;border-radius:6px;text-decoration:none;cursor:pointer}
.button.secondary{background:none;color:var(--ink);box-shadow:inset 0 0 0 1px var(--rule)}
a{color:var(--green);text-underline-offset:.15em}
a:focus-visible,button:focus-visible{outline:2px solid var(--green);outline-offset:3px}
@media (max-width:30rem){main{padding-top:3rem}.code{font-size:3rem}}
</style></head><body><main>
<a class="home" href="${escapeHtml(info.publicUrl ?? '/')}"><img src="/kolf.svg" width="28" height="28" alt="">Kolf</a>
<h1>Join a game of Kolf</h1>
<p>Open Kolf, choose Play Online, and enter this code.</p>
<div class="code" id="code">${escapeHtml(display)}</div>
<div class="actions">
<a class="button" href="${escapeHtml(appLink)}">Open in Kolf</a>
<button class="button secondary" type="button" id="copy">Copy code</button>
</div>
<p class="quiet">The game is on ${escapeHtml(info.serverName)}.${downloads ? ` Don't have Kolf yet? <a href="${escapeHtml(downloads)}">Download it</a>.` : ''}</p>
</main>
<script>
document.getElementById('copy').addEventListener('click',function(){
  var b=this;navigator.clipboard.writeText(${JSON.stringify(code)}).then(function(){b.textContent='Code copied';});
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
        'Content-Security-Policy': "default-src 'none'; img-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; base-uri 'none'; form-action 'none'",
      });
      return;
    }
  }
  send(response, 404, 'text/plain; charset=utf-8', 'not found\n', head);
}
