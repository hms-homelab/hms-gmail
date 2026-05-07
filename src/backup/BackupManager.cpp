#include "BackupManager.h"
#include "gmail/SyncManager.h"
#include "embedding/EmbeddingWorker.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <pqxx/pqxx>
#include <sstream>
#include <thread>
#include <chrono>
#include <iomanip>

static std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

BackupManager::BackupManager(const AppConfig& cfg, MqttPublisher publisher)
    : cfg_(cfg), publish_(std::move(publisher)) {}

bool BackupManager::start(BackupRunOptions opts) {
    std::lock_guard<std::mutex> lock(mu_);
    if (status_.state != BackupState::IDLE && status_.state != BackupState::ERROR)
        return false;
    status_ = BackupStatus{};
    status_.state     = BackupState::BACKUP_RUNNING;
    status_.last_run  = nowIso();

    std::thread([this, opts]{ runBackup(opts); }).detach();
    return true;
}

void BackupManager::stop() {
    // v1.5: no child process to kill — sync runs in a detached thread.
    // Setting state to ERROR will cause the next run() call to be refused
    // until status is reset. The current in-flight sync will finish its
    // current batch then check for interruption on the next iteration.
    std::lock_guard<std::mutex> lock(mu_);
    if (status_.state != BackupState::IDLE)
        status_.state = BackupState::ERROR;
}

BackupStatus BackupManager::status() const {
    std::lock_guard<std::mutex> lock(mu_);
    BackupStatus s = status_;
    // Live DB counts — fast COUNT query on indexed columns
    try {
        pqxx::connection pg(cfg_.db.connString());
        pqxx::work txn(pg);
        auto r = txn.exec(
            "SELECT COUNT(*) AS total,"
            "       COUNT(*) FILTER (WHERE embed_status='done') AS embedded,"
            "       COUNT(*) FILTER (WHERE embed_status='failed') AS failed"
            " FROM emails");
        txn.commit();
        s.db_total    = r[0][0].as<int>();
        s.db_embedded = r[0][1].as<int>();
        s.db_failed   = r[0][2].as<int>();
    } catch (...) {}
    return s;
}

void BackupManager::runBackup(BackupRunOptions opts) {
    publish_("gmail/backup/started", "{\"email\":\"" + cfg_.email + "\"}");

    // Build effective config for this run
    AppConfig run_cfg = cfg_;

    // Compose sync query: start from manual override or config default
    std::string q = opts.sync_query.empty() ? cfg_.gmail_sync_query : opts.sync_query;

    // Append date range modifiers (Gmail format: after:YYYY/M/D before:YYYY/M/D)
    auto toGmailDate = [](const std::string& iso) -> std::string {
        // "2024-01-15" → "2024/1/15"
        if (iso.size() < 10) return "";
        std::string d = iso.substr(0, 10);
        for (char& c : d) if (c == '-') c = '/';
        return d;
    };
    if (!opts.after_date.empty()) {
        auto gd = toGmailDate(opts.after_date);
        if (!gd.empty()) { if (!q.empty()) q += ' '; q += "after:" + gd; }
    }
    if (!opts.before_date.empty()) {
        auto gd = toGmailDate(opts.before_date);
        if (!gd.empty()) { if (!q.empty()) q += ' '; q += "before:" + gd; }
    }
    run_cfg.gmail_sync_query = q;
    run_cfg.max_messages     = opts.max_messages;

    // --- Sync (full or incremental via Gmail API) ---
    SyncManager syncer(run_cfg);
    SyncStats sync_stats;

    if (opts.skip_sync) {
        std::lock_guard<std::mutex> lock(mu_);
        status_.state = BackupState::PURGE_RUNNING; // skip straight to purge/embed
    }

    if (!opts.skip_sync) try {
        sync_stats = syncer.run([this](const std::string& phase, int done, int total) {
            Json::Value p;
            p["phase"] = phase;
            p["done"]  = done;
            p["total"] = total;
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            publish_("gmail/sync/progress", Json::writeString(wb, p));

            if (phase == "sync") {
                std::lock_guard<std::mutex> lock(mu_);
                status_.downloaded = done;
                status_.total      = total;
            } else if (phase == "batch") {
                std::lock_guard<std::mutex> lock(mu_);
                status_.current_batch = done;
                status_.total_batches = total;
            }
        });
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        status_.state      = BackupState::ERROR;
        status_.last_error = e.what();
        publish_("gmail/backup/error", "{\"message\":\"" + std::string(e.what()) + "\"}");
        return;
    }  // end if (!opts.skip_sync)

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!opts.skip_sync) {
            status_.downloaded = sync_stats.indexed;
            status_.skipped    = sync_stats.skipped;
            status_.fetched    = sync_stats.fetched;
        }
        status_.state = BackupState::PURGE_RUNNING;
    }

    // --- Purge old messages ---
    int purged = 0;
    if (opts.run_purge) {
        try {
            purged = syncer.purge([this](const std::string& phase, int done, int total) {
                Json::Value p;
                p["phase"] = phase;
                p["done"]  = done;
                p["total"] = total;
                Json::StreamWriterBuilder wb; wb["indentation"] = "";
                publish_("gmail/purge/progress", Json::writeString(wb, p));
            }, opts.purge_only_embedded);
        } catch (const std::exception& e) {
            std::cerr << "[BackupManager] purge error: " << e.what() << "\n";
        }
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.purged = purged;
        status_.state  = BackupState::INDEXING;
    }
    publish_("gmail/backup/purge_complete",
        "{\"purged\":" + std::to_string(purged) + "}");

    // --- Embed unembedded emails ---
    EmbedStats embed_stats;
    if (opts.run_embedding) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            status_.state = BackupState::EMBEDDING;
        }

        EmbeddingWorker embedder(run_cfg);
        embed_stats = embedder.run(
        [this](int done, int errors, int total) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                status_.embed_done   = done;
                status_.embed_errors = errors;
                status_.embed_total  = total;
            }
            Json::Value p;
            p["done"]   = done;
            p["errors"] = errors;
            p["total"]  = total;
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            publish_("gmail/embed/progress", Json::writeString(wb, p));
        },
        [this]() {
            std::lock_guard<std::mutex> lock(mu_);
            return status_.state == BackupState::ERROR;
        });
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.state = BackupState::IDLE;
    }

    Json::Value complete;
    complete["email"]       = cfg_.email;
    complete["incremental"] = sync_stats.incremental;
    complete["fetched"]     = sync_stats.fetched;
    complete["indexed"]     = sync_stats.indexed;
    complete["skipped"]     = sync_stats.skipped;
    complete["deleted"]     = sync_stats.deleted;
    complete["purged"]      = purged;
    complete["embedded"]    = embed_stats.embedded;
    complete["embed_err"]   = embed_stats.errors;
    complete["errors"]      = sync_stats.errors;
    Json::StreamWriterBuilder b; b["indentation"] = "";

    publish_("gmail/embed/complete",
        "{\"embedded\":" + std::to_string(embed_stats.embedded) +
        ",\"errors\":"   + std::to_string(embed_stats.errors) + "}");
    publish_("gmail/backup/complete", Json::writeString(b, complete));
}

