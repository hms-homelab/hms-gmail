#pragma once
#include "GmailClient.h"
#include "OAuthManager.h"
#include "config/AppConfig.h"
#include "indexer/Indexer.h"
#include <pqxx/pqxx>
#include <functional>
#include <string>

struct SyncStats {
    int fetched   = 0;   // message IDs returned by listMessageIds / listHistory
    int batches   = 0;   // sync_batches enqueued
    int indexed   = 0;   // rows inserted into emails
    int skipped   = 0;   // already in DB
    int deleted   = 0;   // deleted via listHistory
    int errors    = 0;
    bool incremental = false;
};

class SyncManager {
public:
    using ProgressCb = std::function<void(const std::string& phase, int done, int total)>;

    explicit SyncManager(const AppConfig& cfg);

    // Run full or incremental sync.
    // Full if no historyId in sync_state; incremental otherwise.
    // On restart, resumes pending sync_batches from a previous full sync.
    SyncStats run(const ProgressCb& progress_cb = nullptr);

    // Trash messages older than cfg.purge_older_than_days.
    // Returns count of trashed messages.
    int purge(const ProgressCb& progress_cb = nullptr);

private:
    SyncStats runFull(GmailClient& gmail, pqxx::connection& pg,
                      const ProgressCb& cb);
    SyncStats runIncremental(GmailClient& gmail, pqxx::connection& pg,
                             const std::string& history_id,
                             const ProgressCb& cb);
    SyncStats processBatches(GmailClient& gmail, pqxx::connection& pg,
                             const ProgressCb& cb);

    void saveSyncState(pqxx::connection& pg, const std::string& history_id, int total);
    std::string loadHistoryId(pqxx::connection& pg);

    AppConfig cfg_;
};
