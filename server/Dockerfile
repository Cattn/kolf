# SPDX-License-Identifier: GPL-2.0-or-later
# Build from the repository root: docker build -f server/Dockerfile -t kolf-server:TAG .
FROM node:22-alpine

ENV NODE_ENV=production \
    KOLF_BIND=0.0.0.0 \
    KOLF_PORT=3011

WORKDIR /app/server
COPY server/package.json server/package-lock.json ./
RUN npm ci --omit=dev && npm cache clean --force

COPY server/ ./
COPY courses/ /app/courses/

USER node
EXPOSE 3011
STOPSIGNAL SIGTERM
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1:3011/healthz || exit 1

CMD ["node", "main.ts"]
