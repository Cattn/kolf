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
| `deploy/compose.yaml` | Runs the image on `127.0.0.1:3011`, 11-minute stop grace for draining, 7 rotated log files |
| `deploy/systemd/kolf-server.service` | Alternative to Docker using the host's Node 22.18+ |
| `deploy/kolf.env.example` | Server settings; copy to `deploy/kolf.env` (ignored by git) |
| `deploy/nginx/kolf.pro.conf` | nginx site: HTTP→HTTPS, `/ws` WebSocket proxy, `/healthz`, `/status`, `/join/`, static site |
| `deploy/site/` | Landing page, privacy note, styles |

## First-time setup

These steps change the server, so the owner runs them (or approves each one).

1. **DNS.** `kolf.pro` (and optionally `www.kolf.pro`) A/AAAA records point at the
   server. With Cloudflare in front: proxy the records, turn **WebSockets** on,
   and use SSL mode **Full (strict)**. Cloudflare closes idle WebSockets after
   100 s; Kolf pings every 5 s, so that's fine.
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
   sudo certbot certonly --webroot -w /var/www/letsencrypt -d kolf.pro -d www.kolf.pro
   ```
   (Until the certificate exists, temporarily comment out the two `listen 443`
   server blocks so `nginx -t` passes.)
5. **Static site and nginx:**
   ```sh
   sudo cp deploy/site/* /var/www/kolf.pro/
   sudo cp deploy/nginx/kolf.pro.conf /etc/nginx/sites-available/kolf.pro
   sudo ln -s /etc/nginx/sites-available/kolf.pro /etc/nginx/sites-enabled/kolf.pro
   sudo nginx -t && sudo systemctl reload nginx
   ```
6. **Start the server**, either with Docker:
   ```sh
   cd ~/kolf-server/deploy && KOLF_TAG=$(git rev-parse --short HEAD) docker compose up -d --build
   ```
   or with systemd (edit paths in the unit first):
   ```sh
   (cd ~/kolf-server/server && npm ci --omit=dev)
   sudo cp ~/kolf-server/deploy/systemd/kolf-server.service /etc/systemd/system/
   sudo systemctl daemon-reload && sudo systemctl enable --now kolf-server
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
cd ~/kolf-server && git pull
cd deploy && KOLF_TAG=$(git rev-parse --short HEAD) docker compose up -d --build
```

`docker compose up` replaces the container with SIGTERM: the old server tells
players "kolf.pro is restarting for an update", refuses new lobbies, and lets
running matches finish for up to `KOLF_DRAIN_SECONDS` (600) before it exits.
With systemd: `git pull && (cd server && npm ci --omit=dev) && sudo systemctl restart kolf-server`.

Deploy the server when the protocol or the shipped course catalog changes, and
update `KOLF_LATEST_CLIENT_VERSION`/`KOLF_LATEST_CLIENT_URL` after publishing a
client release.

## Draining without deploying

```sh
docker compose stop            # or: sudo systemctl stop kolf-server
```

A second stop (or `docker compose kill`) ends it immediately.

## Logs

```sh
docker compose logs -f --since 1h kolf-server     # or: journalctl -u kolf-server -f
```

Lines are JSON (`lobby.created`, `match.finished`, `request.rejected`, …) and
contain random IDs only. Docker keeps 7 × 20 MB; for systemd set
`MaxRetentionSec=7day` in `journald.conf` if the host keeps more. Set
`KOLF_LOG_DEBUG=1` only while investigating a problem.

## Rolling back

Each deploy is tagged with its commit. To return to an earlier image:

```sh
docker image ls kolf-server
cd ~/kolf-server/deploy && KOLF_TAG=<previous-tag> docker compose up -d --no-build
```
