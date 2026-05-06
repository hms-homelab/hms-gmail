#include "SyncManager.h"
#include "mime/MimeParser.h"
#include <pqxx/pqxx>
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <sstream>

namespace {

// Chunk a vector into groups of at most chunk_size
template<typename T>
std::vector<std::vector<T>> chunk(const std::vector<T>& v, int chunk_size) {
    std::vector<std::vector<T>> result;
    for (size_t i = 0; i < v.size(); i += chunk_size)
        result.push_back({v.begin() + i,
                          v.begin() + std::min(i + (size_t)chunk_size, v.size())});
    return result;
}

} // namespace

SyncManager::SyncManager(const AppConfig& cfg) : cfg_(cfg) {}

std::string SyncManager::loadHistoryId(pqxx::connection& pg) {
    try {
        pqxx::work txn(pg);
        auto r = txn.exec_params(
            "SELECT history_id FROM sync_state WHERE email = $1", cfg_.email);
        txn.commit();
        if (r.empty() || r[0][0].is_null()) return "";
        return r[0][0].as<std::string>();
    } catch (...) { return ""; }
}

void SyncManager::saveSyncState(pqxx::connection& pg,
                                 const std::string& history_id, int total) {
    pqxx::work txn(pg);
    txn.exec_params(
        R"(INSERT INTO sync_state (email, history_id, last_sync_at, total_messages)
           VALUES ($1, $2, now(), $3)
           ON CONFLICT (email) DO UPDATE
               SET history_id     = EXCLUDED.history_id,
                   last_sync_at   = EXCLUDED.last_sync_at,
                   total_messages = EXCLUDED.total_messages)",
        cfg_.email, history_id, total);
    txn.commit();
}

SyncStats SyncManager::run(const ProgressCb& progress_cb) {
    OAuthManager oauth(cfg_.gmail_oauth_file);
    GmailClient  gmail(oauth);
    pqxx::connection pg(cfg_.db.connString());

    std::string history_id = loadHistoryId(pg);

    // If there are pending/in_progress batches from a crashed full sync, resume them
    {
        pqxx::work txn(pg);
        auto r = txn.exec(
            "SELECT count(*) FROM sync_batches WHERE status IN ('pending','in_progress')");
        txn.commit();
        int pending = r[0][0].as<int>();
        if (pending > 0) {
            std::cerr << "[SyncManager] resuming " << pending << " pending batches\n";
            // Reset in_progress → pending (worker died mid-batch)
            {
                pqxx::work fix(pg);
                fix.exec("UPDATE sync_batches SET status='pending' WHERE status='in_progress'");
                fix.commit();
            }
            return processBatches(gmail, pg, progress_cb);
        }
    }

    if (history_id.empty()) {
        return runFull(gmail, pg, progress_cb);
    } else {
        return runIncremental(gmail, pg, history_id, progress_cb);
    }
}

SyncStats SyncManager::runFull(GmailClient& gmail, pqxx::connection& pg,
                                const ProgressCb& cb) {
    SyncStats stats;
    if (cb) cb("list_ids", 0, 0);

    // 1. Fetch all message IDs
    auto all_ids = gmail.listMessageIds(cfg_.gmail_sync_query,
        [&](int n) { if (cb) cb("list_ids", n, 0); });
    stats.fetched = static_cast<int>(all_ids.size());
    std::cerr << "[SyncManager] full sync: " << stats.fetched << " IDs from Gmail\n";

    // 2. Find which gmail_ids are already in PG
    std::unordered_set<std::string> existing;
    {
        pqxx::work txn(pg);
        auto r = txn.exec("SELECT gmail_id FROM emails WHERE gmail_id IS NOT NULL");
        for (const auto& row : r)
            existing.insert(row[0].as<std::string>());
        txn.commit();
    }

    // 3. Build missing list
    std::vector<std::string> missing;
    missing.reserve(all_ids.size());
    for (const auto& id : all_ids)
        if (!existing.count(id)) missing.push_back(id);

    std::cerr << "[SyncManager] missing: " << missing.size()
              << " (existing: " << existing.size() << ")\n";

    if (missing.empty()) {
        // Nothing to download — capture current historyId so next run is incremental
        auto history_id = gmail.getHistoryId();
        if (!history_id.empty())
            saveSyncState(pg, history_id, static_cast<int>(existing.size()));
        return stats;
    }

    // 4. Chunk into batches and enqueue in sync_batches
    auto batches = chunk(missing, cfg_.gmail_batch_size);
    stats.batches = static_cast<int>(batches.size());

    // Clear any old failed batches first
    {
        pqxx::work txn(pg);
        txn.exec("DELETE FROM sync_batches WHERE status = 'failed'");
        txn.commit();
    }

    {
        pqxx::work txn(pg);
        for (int i = 0; i < static_cast<int>(batches.size()); ++i) {
            const auto& b = batches[i];
            // Build TEXT[] literal for message_ids
            std::string ids_arr = "{";
            for (size_t j = 0; j < b.size(); ++j) {
                if (j) ids_arr += ",";
                ids_arr += txn.quote(b[j]);
            }
            ids_arr += "}";

            txn.exec_params(
                R"(INSERT INTO sync_batches
                    (batch_idx, batch_start, batch_end, status, message_ids)
                   VALUES ($1, $2, $3, 'pending', $4::text[]))",
                i,
                i * cfg_.gmail_batch_size,
                i * cfg_.gmail_batch_size + static_cast<int>(b.size()),
                ids_arr);
        }
        txn.commit();
    }

    if (cb) cb("sync", 0, static_cast<int>(missing.size()));

    auto batch_stats = processBatches(gmail, pg, cb);
    stats.indexed  = batch_stats.indexed;
    stats.skipped  = batch_stats.skipped;
    stats.errors   = batch_stats.errors;

    // Capture current historyId so next run is incremental
    try {
        auto history_id = gmail.getHistoryId();
        if (!history_id.empty())
            saveSyncState(pg, history_id, stats.fetched);
    } catch (const std::exception& e) {
        std::cerr << "[SyncManager] warning: could not save historyId: " << e.what() << "\n";
    }

    return stats;
}

SyncStats SyncManager::processBatches(GmailClient& gmail, pqxx::connection& pg,
                                       const ProgressCb& cb) {
    SyncStats stats;
    Indexer indexer(cfg_);

    int total_done = 0;
    int total_msgs = 0;
    {
        pqxx::work txn(pg);
        auto r = txn.exec("SELECT count(*) FROM sync_batches WHERE status='pending'");
        total_msgs = r[0][0].as<int>() * cfg_.gmail_batch_size;
        txn.commit();
    }

    while (true) {
        // Grab next pending batch
        int batch_id = -1;
        std::vector<std::string> ids;

        {
            pqxx::work txn(pg);
            auto r = txn.exec(
                "SELECT id, message_ids FROM sync_batches "
                "WHERE status='pending' ORDER BY batch_idx LIMIT 1 FOR UPDATE SKIP LOCKED");
            if (r.empty()) { txn.commit(); break; }

            batch_id = r[0][0].as<int>();
            auto arr = r[0][1];
            if (!arr.is_null()) {
                // pqxx returns text[] as a string like {id1,id2,...}
                std::string raw = arr.as<std::string>();
                // Strip { }
                if (raw.size() > 2) {
                    raw = raw.substr(1, raw.size() - 2);
                    std::istringstream ss(raw);
                    std::string tok;
                    while (std::getline(ss, tok, ','))
                        if (!tok.empty()) ids.push_back(tok);
                }
            }

            txn.exec_params(
                "UPDATE sync_batches SET status='in_progress' WHERE id=$1", batch_id);
            txn.commit();
        }

        if (ids.empty()) {
            pqxx::work txn(pg);
            txn.exec_params("UPDATE sync_batches SET status='failed' WHERE id=$1", batch_id);
            txn.commit();
            continue;
        }

        try {
            // batchGetRaw returns RawMessage with base64url raw field
            auto raw_msgs = gmail.batchGetRaw(ids);

            // Parse each message
            std::vector<GmailEmail> parsed_emails;
            parsed_emails.reserve(raw_msgs.size());
            for (const auto& rm : raw_msgs) {
                try {
                    GmailEmail ge;
                    ge.parsed    = MimeParser::parseBase64Url(rm.raw_rfc822);
                    ge.gmail_id  = rm.id;
                    ge.thread_id = rm.thread_id;
                    ge.labels    = rm.label_ids;
                    parsed_emails.push_back(std::move(ge));
                } catch (const std::exception& e) {
                    std::cerr << "[SyncManager] parse error for " << rm.id
                              << ": " << e.what() << "\n";
                    ++stats.errors;
                }
            }

            // Insert into PG
            auto idx_stats = indexer.insertGmailBatch(parsed_emails,
                [&](int done, int /*total*/) {
                    total_done = done;
                    if (cb) cb("sync", total_done, total_msgs);
                });

            stats.indexed += idx_stats.indexed;
            stats.skipped += idx_stats.skipped;
            stats.errors  += idx_stats.errors;

            // Mark batch complete
            pqxx::work txn(pg);
            txn.exec_params(
                "UPDATE sync_batches SET status='complete', completed_at=now(), "
                "indexed_count=$2, error_count=$3 WHERE id=$1",
                batch_id, idx_stats.indexed, idx_stats.errors);
            txn.commit();

        } catch (const std::exception& e) {
            std::cerr << "[SyncManager] batch " << batch_id << " failed: " << e.what() << "\n";

            pqxx::work txn(pg);
            int retry = 0;
            auto r = txn.exec_params("SELECT retry_count FROM sync_batches WHERE id=$1", batch_id);
            if (!r.empty()) retry = r[0][0].as<int>();
            if (retry < 3) {
                txn.exec_params(
                    "UPDATE sync_batches SET status='pending', retry_count=$2 WHERE id=$1",
                    batch_id, retry + 1);
            } else {
                txn.exec_params(
                    "UPDATE sync_batches SET status='failed' WHERE id=$1", batch_id);
                ++stats.errors;
            }
            txn.commit();
        }
    }

    return stats;
}

SyncStats SyncManager::runIncremental(GmailClient& gmail, pqxx::connection& pg,
                                       const std::string& history_id,
                                       const ProgressCb& cb) {
    SyncStats stats;
    stats.incremental = true;

    if (cb) cb("history", 0, 0);

    HistoryDelta delta = gmail.listHistory(history_id);
    std::cerr << "[SyncManager] incremental: +" << delta.added_ids.size()
              << " -" << delta.deleted_ids.size() << "\n";

    // Delete removed messages
    if (!delta.deleted_ids.empty()) {
        pqxx::work txn(pg);
        std::string in_list;
        for (size_t i = 0; i < delta.deleted_ids.size(); ++i) {
            if (i) in_list += ',';
            in_list += txn.quote(delta.deleted_ids[i]);
        }
        auto r = txn.exec("DELETE FROM emails WHERE gmail_id IN (" + in_list + ")");
        stats.deleted = static_cast<int>(r.affected_rows());
        txn.commit();
    }

    // Fetch and insert new messages in batches
    if (!delta.added_ids.empty()) {
        stats.fetched = static_cast<int>(delta.added_ids.size());
        Indexer indexer(cfg_);
        int total = stats.fetched;
        int done  = 0;

        auto batches = chunk(delta.added_ids, cfg_.gmail_batch_size);
        for (const auto& batch_ids : batches) {
            auto raw_msgs = gmail.batchGetRaw(batch_ids);

            std::vector<GmailEmail> parsed_emails;
            parsed_emails.reserve(raw_msgs.size());
            for (const auto& rm : raw_msgs) {
                try {
                    GmailEmail ge;
                    ge.parsed    = MimeParser::parseBase64Url(rm.raw_rfc822);
                    ge.gmail_id  = rm.id;
                    ge.thread_id = rm.thread_id;
                    ge.labels    = rm.label_ids;
                    parsed_emails.push_back(std::move(ge));
                } catch (const std::exception& e) {
                    std::cerr << "[SyncManager] incremental parse error: " << e.what() << "\n";
                    ++stats.errors;
                }
            }

            auto idx_stats = indexer.insertGmailBatch(parsed_emails,
                [&](int d, int /*t*/) {
                    done += d;
                    if (cb) cb("sync", done, total);
                });
            stats.indexed += idx_stats.indexed;
            stats.skipped += idx_stats.skipped;
            stats.errors  += idx_stats.errors;
        }
    }

    // Save new historyId
    if (!delta.new_history_id.empty()) {
        saveSyncState(pg, delta.new_history_id,
                      stats.fetched + stats.skipped);
    }

    return stats;
}

int SyncManager::purge(const ProgressCb& cb) {
    if (cfg_.purge_older_than_days <= 0) return 0;

    OAuthManager oauth(cfg_.gmail_oauth_file);
    GmailClient  gmail(oauth);

    std::string query = "older_than:" + std::to_string(cfg_.purge_older_than_days) + "d";
    if (cb) cb("purge_list", 0, 0);

    auto ids = gmail.listMessageIds(query,
        [&](int n) { if (cb) cb("purge_list", n, 0); });

    if (ids.empty()) return 0;

    int trashed = 0;
    if (cb) cb("purge", 0, static_cast<int>(ids.size()));

    for (const auto& id : ids) {
        try {
            gmail.trashMessage(id);
            ++trashed;
            if (cb && trashed % 10 == 0)
                cb("purge", trashed, static_cast<int>(ids.size()));
        } catch (const std::exception& e) {
            std::cerr << "[SyncManager] purge error for " << id << ": " << e.what() << "\n";
        }
    }

    return trashed;
}
