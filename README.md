# hms-gmail

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-support-%23FFDD00.svg?logo=buy-me-a-coffee)](https://www.buymeacoffee.com/aamat09)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![status](https://img.shields.io/badge/status-experimental-orange)

Self-hosted Gmail backup, search, and inbox management service. Syncs your Gmail inbox directly via the Gmail API (no intermediary tools), stores emails in PostgreSQL with full-text and semantic (vector) search, and automatically purges old messages on a schedule.

## Features

- **Direct Gmail API sync** — OAuth2 with incremental historyId (subsequent syncs take seconds, not minutes)
- **Batch download** — fetches up to 100 emails per API call
- **Full-text search** — PostgreSQL `tsvector` GIN index
- **Semantic search** — pgvector embeddings via Ollama (nomic-embed-text)
- **Hybrid search** — Reciprocal Rank Fusion combining FTS + vector results
- **Auto-purge** — trashes inbox messages older than N days via Gmail API
- **Scheduled sync** — cron expression (default: Sunday 3am UTC)
- **Angular UI** — search interface, email viewer, thread view, backup status
- **MQTT events** — backup progress, search requests/responses
- **54 unit tests**

## Architecture

```
Gmail API ──OAuth2──► GmailClient ──batch 100──► MimeParser ──► Indexer ──► PostgreSQL
                                                                                  │
                                                                        EmbeddingWorker
                                                                         (Ollama nomic)
                                                                                  │
                                                                            pgvector
                                                                                  │
HTTP :8890 ◄── SearchEngine (FTS + vector + RRF) ◄──────────────────────────────┘
Angular UI ◄── Drogon ◄──────────────────────────────────────────────────────────┘
MQTT ◄──────── BackupManager ◄───────────────────────────────────────────────────┘
```

## Quick Start

### Docker (recommended)

```bash
# Pull the image
docker pull ghcr.io/hms-homelab/hms-gmail:latest

# Run
docker run -d \
  --name hms-gmail \
  -p 8890:8890 \
  -v /path/to/config.yaml:/etc/hms-gmail/config.yaml \
  -v /path/to/oauth.json:/etc/hms-gmail/oauth.json \
  ghcr.io/hms-homelab/hms-gmail:latest
```

### Docker Compose

```yaml
services:
  hms-gmail:
    image: ghcr.io/hms-homelab/hms-gmail:latest
    ports:
      - "8890:8890"
    volumes:
      - ./config.yaml:/etc/hms-gmail/config.yaml
      - ./oauth.json:/etc/hms-gmail/oauth.json
      - gmail_data:/data/gmail
    restart: unless-stopped

volumes:
  gmail_data:
```

### Build from source

```bash
git clone https://github.com/hms-homelab/hms-gmail.git
cd hms-gmail

# Install dependencies (Debian/Ubuntu)
sudo apt-get install -y build-essential cmake \
  libcurl4-openssl-dev libssl-dev libpq-dev libpqxx-dev \
  libjsoncpp-dev libpaho-mqtt-dev libpaho-mqttpp-dev \
  nlohmann-json3-dev libspdlog-dev libsqlite3-dev \
  libdrogon-dev libyaml-cpp-dev libvmime-dev zlib1g-dev libfmt-dev

# Build frontend
cd frontend && npm ci && npx ng build --configuration production && cd ..

# Build C++
mkdir build && cd build
cmake -DBUILD_WITH_WEB=ON ..
make -j$(nproc)
```

## Configuration

Create `/etc/hms-gmail/config.yaml`:

```yaml
port: 8890
email: you@gmail.com
purge_older_than_days: 30
schedule_cron: "0 3 * * 0"   # Sunday 3am UTC

gmail_oauth_file: /etc/hms-gmail/oauth.json
gmail_batch_size: 100
gmail_sync_query: ""          # empty = all mail, or e.g. "newer_than:2y"

ollama_host: http://localhost:11434
embedding_batch_size: 20

mqtt:
  host: localhost
  port: 1883
  user: ""
  pass: ""

db:
  host: localhost
  port: 5432
  name: gmail
  user: gmail_user
  pass: changeme
```

### OAuth setup

hms-gmail reuses an OAuth2 token file in the same format as [Got Your Back (gyb)](https://github.com/GAM-team/got-your-back). The file at `gmail_oauth_file` must contain:

```json
{
  "client_id": "YOUR_CLIENT_ID",
  "client_secret": "YOUR_CLIENT_SECRET",
  "refresh_token": "YOUR_REFRESH_TOKEN",
  "token": "",
  "token_expiry": "",
  "token_uri": "https://oauth2.googleapis.com/token"
}
```

The service auto-refreshes the access token and writes it back to the file.

### PostgreSQL setup

```sql
CREATE DATABASE gmail;
CREATE USER gmail_user WITH PASSWORD 'changeme';
GRANT ALL PRIVILEGES ON DATABASE gmail TO gmail_user;
-- Enable pgvector extension (required for semantic search)
\c gmail
CREATE EXTENSION IF NOT EXISTS vector;
```

The schema is applied automatically on first start.

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/health` | Service health |
| `GET` | `/backup/status` | Sync state and progress |
| `POST` | `/backup/start` | Trigger a sync immediately |
| `POST` | `/backup/stop` | Abort running sync |
| `GET` | `/search?q=&limit=&mode=` | Search emails (mode: fts/vector/hybrid) |
| `GET` | `/emails/:id` | Email detail |
| `GET` | `/threads/:tid` | Thread view |

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `gmail/backup/started` | publish | Sync started |
| `gmail/sync/progress` | publish | `{phase, done, total}` |
| `gmail/backup/purge_complete` | publish | Purge finished |
| `gmail/backup/complete` | publish | Full sync summary |
| `gmail/backup/error` | publish | Error payload |
| `gmail/embed/progress` | publish | Embedding progress |
| `gmail/embed/complete` | publish | Embedding complete |
| `gmail/search/request` | subscribe | `{q, limit}` → triggers search |
| `gmail/search/response` | publish | Search results JSON |

## Docker Tags

| Tag | Description |
|-----|-------------|
| `latest` | Latest stable build from master |
| `v1.5` | v1.5.x releases |
| `v1.5.0` | Specific patch release |
| `sha-xxxxxxx` | Specific commit build |

## Related Projects

- [hms-cpap](https://github.com/hms-homelab/hms-cpap) — ResMed CPAP data collection
- [hms-firetv](https://github.com/hms-homelab/hms-firetv) — Fire TV control
- [hms-portal](https://github.com/hms-homelab/hms-portal) — Homelab dashboard

## License

MIT — see [LICENSE](LICENSE)
