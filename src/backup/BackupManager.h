#pragma once
#include <string>
#include <functional>
#include <mutex>
#include "config/AppConfig.h"

enum class BackupState { IDLE, BACKUP_RUNNING, PURGE_RUNNING, INDEXING, EMBEDDING, ERROR };

struct BackupRunOptions {
    std::string sync_query;       // merged with date range and appended to config default
    std::string after_date;       // YYYY-MM-DD  → Gmail after:YYYY/M/D
    std::string before_date;      // YYYY-MM-DD  → Gmail before:YYYY/M/D
    int         max_messages = 0; // 0 = unlimited
    bool        skip_sync          = false;
    bool        run_purge          = true;
    bool        purge_only_embedded = false;  // only trash Gmail msgs that are already embedded
    bool        run_embedding      = true;
};

struct BackupStatus {
    BackupState state         = BackupState::IDLE;
    int         downloaded    = 0;  // newly indexed this run
    int         skipped       = 0;  // fetched from Gmail but already in DB
    int         fetched       = 0;  // total IDs returned by Gmail API this run
    int         total         = 0;
    int         purged        = 0;
    int         current_batch = 0;
    int         total_batches = 0;
    int         embed_done    = 0;
    int         embed_errors  = 0;
    int         embed_total   = 0;
    // DB-level stats (populated on every status() call)
    int         db_total      = 0;
    int         db_embedded   = 0;
    int         db_failed     = 0;
    std::string last_error;
    std::string last_run;   // ISO8601
    std::string next_run;   // ISO8601
};

class BackupManager {
public:
    using MqttPublisher = std::function<void(const std::string& topic, const std::string& payload)>;

    explicit BackupManager(const AppConfig& cfg, MqttPublisher publisher);

    bool  start(BackupRunOptions opts = {});   // returns false if already running
    void  stop();
    BackupStatus status() const;

private:
    void  runBackup(BackupRunOptions opts);

    AppConfig     cfg_;
    MqttPublisher publish_;

    mutable std::mutex mu_;
    BackupStatus       status_;
};
