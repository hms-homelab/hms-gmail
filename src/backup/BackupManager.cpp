#include "BackupManager.h"
#include "gmail/SyncManager.h"
#include "embedding/EmbeddingWorker.h"
#include <drogon/drogon.h>
#include <json/json.h>
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

bool BackupManager::start() {
    std::lock_guard<std::mutex> lock(mu_);
    if (status_.state != BackupState::IDLE && status_.state != BackupState::ERROR)
        return false;
    status_ = BackupStatus{};
    status_.state     = BackupState::BACKUP_RUNNING;
    status_.last_run  = nowIso();

    std::thread([this]{ runBackup(); }).detach();
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
    return status_;
}

void BackupManager::runBackup() {
    publish_("gmail/backup/started", "{\"email\":\"" + cfg_.email + "\"}");

    // --- Sync (full or incremental via Gmail API) ---
    SyncManager syncer(cfg_);
    SyncStats sync_stats;

    try {
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
            }
        });
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        status_.state      = BackupState::ERROR;
        status_.last_error = e.what();
        publish_("gmail/backup/error", "{\"message\":\"" + std::string(e.what()) + "\"}");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.downloaded = sync_stats.indexed;
        status_.state      = BackupState::PURGE_RUNNING;
    }

    // --- Purge old messages ---
    int purged = 0;
    try {
        purged = syncer.purge([this](const std::string& phase, int done, int total) {
            Json::Value p;
            p["phase"] = phase;
            p["done"]  = done;
            p["total"] = total;
            Json::StreamWriterBuilder wb; wb["indentation"] = "";
            publish_("gmail/purge/progress", Json::writeString(wb, p));
        });
    } catch (const std::exception& e) {
        std::cerr << "[BackupManager] purge error: " << e.what() << "\n";
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.purged = purged;
        status_.state  = BackupState::INDEXING;
    }
    publish_("gmail/backup/purge_complete",
        "{\"purged\":" + std::to_string(purged) + "}");

    // --- Embed unembedded emails ---
    {
        std::lock_guard<std::mutex> lock(mu_);
        status_.state = BackupState::EMBEDDING;
    }

    EmbeddingWorker embedder(cfg_);
    auto embed_stats = embedder.run([this](int done, int total) {
        Json::Value p;
        p["done"]  = done;
        p["total"] = total;
        Json::StreamWriterBuilder wb; wb["indentation"] = "";
        publish_("gmail/embed/progress", Json::writeString(wb, p));
    });

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

