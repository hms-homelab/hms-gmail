#include "Database.h"
#include <sstream>
#include <stdexcept>

Database::Database(const DbConfig& cfg) {
    std::ostringstream ss;
    ss << "host=" << cfg.host
       << " port=" << cfg.port
       << " dbname=" << cfg.name
       << " user=" << cfg.user
       << " password=" << cfg.pass;
    conn_str_ = ss.str();
    conn_ = std::make_unique<pqxx::connection>(conn_str_);
}

void Database::ensureSchema() {
    pqxx::work txn(*conn_);

    txn.exec("CREATE EXTENSION IF NOT EXISTS vector");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS emails (
            id             BIGSERIAL PRIMARY KEY,
            message_id     TEXT UNIQUE NOT NULL,
            thread_id      TEXT,
            from_addr      TEXT,
            to_addrs       TEXT[],
            cc             TEXT[],
            subject        TEXT,
            date           TIMESTAMPTZ,
            labels         TEXT[],
            body_text      TEXT,
            body_html      TEXT,
            has_attachment BOOLEAN DEFAULT false,
            gyb_path       TEXT,
            search_vec     TSVECTOR GENERATED ALWAYS AS (
                to_tsvector('english',
                    coalesce(subject, '') || ' ' ||
                    coalesce(from_addr, '') || ' ' ||
                    coalesce(body_text, ''))
            ) STORED,
            embedding      vector(768)
        )
    )");

    txn.exec("CREATE INDEX IF NOT EXISTS emails_fts_idx  ON emails USING GIN(search_vec)");
    txn.exec("CREATE INDEX IF NOT EXISTS emails_date_idx ON emails(date DESC)");
    txn.exec("CREATE INDEX IF NOT EXISTS emails_from_idx ON emails(from_addr)");
    txn.exec("CREATE INDEX IF NOT EXISTS emails_thread_idx ON emails(thread_id)");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS email_attachments (
            id         BIGSERIAL PRIMARY KEY,
            email_id   BIGINT REFERENCES emails(id) ON DELETE CASCADE,
            filename   TEXT,
            mime_type  TEXT,
            size_bytes INT,
            part_index INT
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS backup_runs (
            id                BIGSERIAL PRIMARY KEY,
            started_at        TIMESTAMPTZ DEFAULT now(),
            completed_at      TIMESTAMPTZ,
            indexed_at        TIMESTAMPTZ,
            emails_downloaded INT,
            emails_purged     INT,
            emails_indexed    INT,
            emails_embedded   INT,
            status            TEXT DEFAULT 'running_backup'
        )
    )");

    // v1.5: gmail_id for incremental historyId sync
    txn.exec("ALTER TABLE emails ADD COLUMN IF NOT EXISTS gmail_id TEXT");
    txn.exec("CREATE UNIQUE INDEX IF NOT EXISTS emails_gmail_id_idx ON emails(gmail_id) WHERE gmail_id IS NOT NULL");

    // v1.5: gmail_attachment_id for lazy attachment download
    txn.exec("ALTER TABLE email_attachments ADD COLUMN IF NOT EXISTS gmail_message_id TEXT");
    txn.exec("ALTER TABLE email_attachments ADD COLUMN IF NOT EXISTS gmail_attachment_id TEXT");

    // v1.5: historyId-based sync state per email account
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_state (
            email          TEXT PRIMARY KEY,
            history_id     TEXT,
            last_sync_at   TIMESTAMPTZ,
            total_messages INT DEFAULT 0
        )
    )");

    // v1.5: persistent batch queue for outage resilience
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_batches (
            id            SERIAL PRIMARY KEY,
            batch_idx     INT NOT NULL,
            batch_start   INT NOT NULL,
            batch_end     INT NOT NULL,
            status        TEXT NOT NULL DEFAULT 'pending',
            message_ids   TEXT[],
            indexed_count INT DEFAULT 0,
            error_count   INT DEFAULT 0,
            created_at    TIMESTAMPTZ DEFAULT now(),
            completed_at  TIMESTAMPTZ,
            retry_count   INT DEFAULT 0
        )
    )");
    txn.exec("CREATE INDEX IF NOT EXISTS sync_batches_status_idx ON sync_batches(status)");

    // v1.5: attachment download queue
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS attachment_downloads (
            id                  SERIAL PRIMARY KEY,
            email_attachment_id INT REFERENCES email_attachments(id) ON DELETE CASCADE,
            gmail_message_id    TEXT NOT NULL,
            gmail_attachment_id TEXT NOT NULL,
            status              TEXT NOT NULL DEFAULT 'pending',
            priority            INT DEFAULT 0,
            size_bytes          INT,
            created_at          TIMESTAMPTZ DEFAULT now(),
            updated_at          TIMESTAMPTZ DEFAULT now(),
            completed_at        TIMESTAMPTZ,
            retry_count         INT DEFAULT 0,
            last_error          TEXT
        )
    )");
    txn.exec("CREATE INDEX IF NOT EXISTS att_dl_status_priority_idx ON attachment_downloads(status, priority DESC, created_at)");

    // v1.5: attachment binary storage (lazy, on first request)
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS attachment_data (
            attachment_download_id INT PRIMARY KEY REFERENCES attachment_downloads(id) ON DELETE CASCADE,
            data BYTEA NOT NULL
        )
    )");

    txn.commit();
}
