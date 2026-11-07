# Deploying kolf.pro

The Kolf online server is one Node process that keeps everything in memory. On
`kolf.pro` it runs behind the host's existing nginx, which terminates TLS and
serves the small static site in `deploy/site/`.

```text
player ── wss://kolf.pro/ws ──► nginx :443 ──► 127.0.0.1:3011  kolf-server
browser ─ https://kolf.pro/  ─► nginx :443 ──► /var/www/kolf.pro (static site)
                  /join/CODE, /status, /healthz ──► 127.0.0.1:3011
```

Files:

| File | Purpose |
| --- | --- |
| `server/Dockerfile`, `.dockerignore` | Image with `server/` and `courses/` (the catalog must ship with the server) |
| `deploy/systemd/kolf-server.service` | Runs the server with the host's Node 22.18+ on `127.0.0.1:3011` (the setup used on kolf.pro) |
| `deploy/compose.yaml` | Docker alternative: same port, 11-minute stop grace for draining, 7 rotated log files |
| `deploy/kolf.env.example` | Server settings; copy to `deploy/kolf.env` (ignored by git) |
| `deploy/nginx/kolf.pro.conf` | nginx site: HTTP→HTTPS, `/ws` WebSocket proxy, `/healthz`, `/status`, `/join/`, `/download` redirect to GitHub releases, static site |
| `deploy/site/` | Landing page, privacy note, styles, icon |

## First-time setup

These steps change the server, so the owner runs them (or approves each one).

1. **DNS.** The `kolf.pro` A record points at the server. With Cloudflare in
   front: proxy the record, turn **WebSockets** on, and use SSL mode
   **Full (strict)**. Cloudflare closes idle WebSockets after 100 s; Kolf pings
   every 5 s, so that's fine. A proxied record only carries HTTP(S), so the
   Minecraft server on this host then needs its own DNS-only name.
2. **Stop the old server.** An older build runs as `node main.ts` from
   `~/kolf/kolf/server` on `0.0.0.0:3011`. It holds the port and is reachable
   without TLS. Stop it (and whatever starts it) before starting the new one.
3. **Check out the code** (any path; the examples use `~/kolf-server`):
   ```sh
   git clone -b multi-proto https://github.com/Cattn/kolf.git ~/kolf-server
   cd ~/kolf-server && cp deploy/kolf.env.example deploy/kolf.env && $EDITOR deploy/kolf.env
   ```
4. **Certificate.** Serve the ACME challenge over HTTP first, then request it:
   ```sh
   sudo mkdir -p /var/www/letsencrypt /var/www/kolf.pro
   sudo certbot certonly --webroot -w /var/www/letsencrypt -d kolf.pro --deploy-hook "systemctl reload nginx"
   ```
   (Until the certificate exists, temporarily comment out the `listen 443`
   server block so `nginx -t` passes. The certificate covers `kolf.pro` only.)
5. **Static site and nginx:**
   ```sh
   sudo cp deploy/site/* /var/www/kolf.pro/
   sudo cp deploy/nginx/kolf.pro.conf /etc/nginx/sites-available/kolf.pro
   sudo ln -s /etc/nginx/sites-available/kolf.pro /etc/nginx/sites-enabled/kolf.pro
   sudo nginx -t && sudo systemctl reload nginx
   ```
6. **Start the server** with systemd (check the user, paths, and Node version in the unit first;
   with systemd, `KOLF_TRUST_PROXY=127.0.0.1` is enough):
   ```sh
   (cd ~/kolf-server/server && npm ci --omit=dev)
   sudo cp ~/kolf-server/deploy/systemd/kolf-server.service /etc/systemd/system/
   sudo systemctl daemon-reload && sudo systemctl enable --now kolf-server
   ```
   Docker alternative (also trust `172.16.0.0/12`, the Docker bridge):
   ```sh
   cd ~/kolf-server/deploy && KOLF_TAG=$(git rev-parse --short HEAD) sudo -E docker compose up -d --build
   ```
7. **Check it:**
   ```sh
   curl -s https://kolf.pro/healthz      # ok
   curl -s https://kolf.pro/status       # {"name":"kolf.pro",...}
   ```
   Then open Kolf → Play Online. It should connect to kolf.pro with no typing.
8. **Uptime check:** point any free external monitor at `https://kolf.pro/healthz`.

## Deploying a new version

```sh
cd ~/kolf-server && git pull && (cd server && npm ci --omit=dev)
sudo cp deploy/site/* /var/www/kolf.pro/
sudo systemctl restart kolf-server
```

The restart sends SIGTERM: the old server tells players "kolf.pro is restarting
for an update", refuses new lobbies, and lets running matches finish for up to
`KOLF_DRAIN_SECONDS` (600) before it exits. With Docker, `docker compose up -d
--build` replaces the container the same way.

Deploy the server when the protocol or the shipped course catalog changes, and
update `KOLF_LATEST_CLIENT_VERSION`/`KOLF_LATEST_CLIENT_URL` after publishing a
client release.

## Draining without deploying

```sh
sudo systemctl stop kolf-server        # Docker: docker compose stop
```

`sudo systemctl kill -s INT kolf-server` (or `docker compose kill`) ends it immediately.

## Logs

```sh
journalctl -u kolf-server -f        # Docker: docker compose logs -f kolf-server
```

Lines are JSON (`lobby.created`, `match.finished`, `request.rejected`, …) and
contain random IDs only. For systemd set `MaxRetentionSec=7day` in
`journald.conf` if the host keeps more; Docker keeps 7 × 20 MB. Set
`KOLF_LOG_DEBUG=1` only while investigating a problem.

## Rolling back

```sh
cd ~/kolf-server && git checkout <previous-commit> && (cd server && npm ci --omit=dev)
sudo systemctl restart kolf-server
```

With Docker, each deploy is tagged with its commit: `KOLF_TAG=<previous-tag> docker compose up -d --no-build`.
