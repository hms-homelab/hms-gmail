#include "Indexer.h"
#include "mime/MimeParser.h"
#include <pqxx/pqxx>
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <unordered_set>

namespace {

std::string rfc2822ToIso(const std::string& raw) {
    if (raw.empty()) return "";
    try {
        std::string s = raw;
        auto colon = s.find(':');
        if (colon != std::string::npos && colon < 10)
            s = s.substr(colon + 1);
        vmime::datetime dt;
        dt.parse(s);
        std::ostringstream ss;
        ss << std::setfill('0')
           << std::setw(4) << dt.getYear()  << "-"
           << std::setw(2) << dt.getMonth() << "-"
           << std::setw(2) << dt.getDay()   << "T"
           << std::setw(2) << dt.getHour()  << ":"
           << std::setw(2) << dt.getMinute()<< ":"
           << std::setw(2) << dt.getSecond()<< "Z";
        return ss.str();
    } catch (...) { return ""; }
}

std::string pgArray(pqxx::work& txn, const std::vector<std::string>& v) {
    if (v.empty()) return "{}";
    std::string out = "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += txn.quote(v[i]);
    }
    out += "}";
    return out;
}

struct SqRow { std::string filename; };

} // namespace

static constexpr int BATCH = 500;

Indexer::Indexer(const AppConfig& cfg) : cfg_(cfg) {}

IndexerStats Indexer::run(const ProgressCb& progress_cb) {
    IndexerStats stats;

    std::string db_path = cfg_.backup_dir + "/msg-db.sqlite";
    sqlite3* sq = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &sq, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "[Indexer] cannot open sqlite: " << db_path << "\n";
        return stats;
    }

    // Count
    sqlite3_stmt* cs = nullptr;
    sqlite3_prepare_v2(sq, "SELECT count(*) FROM messages", -1, &cs, nullptr);
    if (sqlite3_step(cs) == SQLITE_ROW) stats.total = sqlite3_column_int(cs, 0);
    sqlite3_finalize(cs);

    if (stats.total == 0) { sqlite3_close(sq); return stats; }

    // Load all filenames into memory (~8 MB for 154k rows)
    std::vector<SqRow> rows;
    rows.reserve(stats.total);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(sq,
        "SELECT message_filename FROM messages ORDER BY message_num",
        -1, &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        rows.push_back({ fn ? fn : "" });
    }
    sqlite3_finalize(st);
    sqlite3_close(sq);

    // Open PG
    pqxx::connection pg(cfg_.db.connString());

    int done = 0;
    int total = static_cast<int>(rows.size());

    for (int start = 0; start < total; start += BATCH) {
        int end = std::min(start + BATCH, total);

        // --- Parse .eml files for this batch ---
        struct Item {
            ParsedEmail em;
            std::string eml_path;
        };
        std::vector<Item> items;
        items.reserve(end - start);

        for (int i = start; i < end; ++i) {
            std::string eml_path = cfg_.backup_dir + "/" + rows[i].filename;
            auto em = MimeParser::parseFile(eml_path);
            if (em.message_id.empty()) {
                ++stats.errors;
                ++done;
                continue;
            }
            items.push_back({ std::move(em), std::move(eml_path) });
        }

        if (items.empty()) {
            if (progress_cb) progress_cb(done, total);
            continue;
        }

        try {
            pqxx::work txn(pg);

            // --- Find already-indexed message_ids in one query ---
            std::string in_list;
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) in_list += ',';
                in_list += txn.quote(items[i].em.message_id);
            }
            auto existing_rows = txn.exec(
                "SELECT message_id FROM emails WHERE message_id IN (" + in_list + ")");
            std::unordered_set<std::string> existing;
            for (const auto& r : existing_rows)
                existing.insert(r[0].as<std::string>());

            // --- Insert new emails ---
            for (auto& item : items) {
                if (existing.count(item.em.message_id)) {
                    ++stats.skipped;
                    ++done;
                    continue;
                }

                std::string date_iso = rfc2822ToIso(item.em.date_str);
                std::optional<std::string> date_param;
                if (!date_iso.empty()) date_param = date_iso;

                auto res = txn.exec_params(
                    R"(INSERT INTO emails
                        (message_id, from_addr, to_addrs, cc, subject, date,
                         body_text, body_html, has_attachment, gyb_path)
                       VALUES ($1,$2,$3::text[],$4::text[],$5,
                               $6::timestamptz,$7,$8,$9,$10)
                       RETURNING id)",
                    item.em.message_id,
                    item.em.from_addr,
                    pgArray(txn, item.em.to_addrs),
                    pgArray(txn, item.em.cc),
                    item.em.subject,
                    date_param,
                    item.em.body_text,
                    item.em.body_html,
                    item.em.has_attachment,
                    item.eml_path);

                if (res.empty()) { ++stats.errors; ++done; continue; }
                long email_db_id = res[0][0].as<long>();

                for (const auto& att : item.em.attachments) {
                    txn.exec_params(
                        "INSERT INTO email_attachments "
                        "(email_id, filename, mime_type, size_bytes, part_index) "
                        "VALUES ($1,$2,$3,$4,$5)",
                        email_db_id, att.filename, att.mime_type,
                        att.size_bytes, att.part_index);
                }

                ++stats.indexed;
                ++done;
            }

            txn.commit();

        } catch (const std::exception& e) {
            std::cerr << "[Indexer] batch error at " << start << ": " << e.what() << "\n";
            // Count unprocessed items in this batch as errors and move on
            int unprocessed = (end - start) - static_cast<int>(
                std::count_if(items.begin(), items.end(), [](const Item&){ return true; }));
            stats.errors += unprocessed;
            done = end;
        }

        if (progress_cb) progress_cb(done, total);
    }

    return stats;
}

IndexerStats Indexer::insertGmailBatch(const std::vector<GmailEmail>& emails,
                                       const ProgressCb& progress_cb) {
    IndexerStats stats;
    if (emails.empty()) return stats;

    stats.total = static_cast<int>(emails.size());
    pqxx::connection pg(cfg_.db.connString());

    // Dedup: check which gmail_ids already exist
    {
        pqxx::work txn(pg);
        std::string in_list;
        for (size_t i = 0; i < emails.size(); ++i) {
            if (i) in_list += ',';
            in_list += txn.quote(emails[i].gmail_id);
        }
        auto existing_rows = txn.exec(
            "SELECT gmail_id FROM emails WHERE gmail_id IN (" + in_list + ")");
        std::unordered_set<std::string> existing;
        for (const auto& r : existing_rows)
            existing.insert(r[0].as<std::string>());
        txn.abort();

        int done = 0;
        pqxx::work ins(pg);

        for (const auto& ge : emails) {
            if (existing.count(ge.gmail_id)) {
                ++stats.skipped;
                ++done;
                if (progress_cb) progress_cb(done, stats.total);
                continue;
            }

            const auto& em = ge.parsed;
            if (em.message_id.empty() && ge.gmail_id.empty()) {
                ++stats.errors;
                ++done;
                continue;
            }

            std::string date_iso = rfc2822ToIso(em.date_str);
            std::optional<std::string> date_param;
            if (!date_iso.empty()) date_param = date_iso;

            // Build labels array
            std::string labels_arr = pgArray(ins, ge.labels);

            try {
                auto res = ins.exec_params(
                    R"(INSERT INTO emails
                        (message_id, thread_id, from_addr, to_addrs, cc, subject, date,
                         body_text, body_html, has_attachment, gmail_id, labels)
                       VALUES ($1,$2,$3,$4::text[],$5::text[],$6,
                               $7::timestamptz,$8,$9,$10,$11,$12::text[])
                       ON CONFLICT (message_id) DO UPDATE
                           SET gmail_id = EXCLUDED.gmail_id,
                               labels   = EXCLUDED.labels
                       RETURNING id)",
                    em.message_id.empty() ? ge.gmail_id : em.message_id,
                    ge.thread_id,
                    em.from_addr,
                    pgArray(ins, em.to_addrs),
                    pgArray(ins, em.cc),
                    em.subject,
                    date_param,
                    em.body_text,
                    em.body_html,
                    em.has_attachment,
                    ge.gmail_id,
                    labels_arr);

                if (res.empty()) { ++stats.errors; ++done; continue; }
                long email_db_id = res[0][0].as<long>();

                for (const auto& att : em.attachments) {
                    ins.exec_params(
                        "INSERT INTO email_attachments "
                        "(email_id, filename, mime_type, size_bytes, part_index, gmail_message_id) "
                        "VALUES ($1,$2,$3,$4,$5,$6) "
                        "ON CONFLICT DO NOTHING",
                        email_db_id, att.filename, att.mime_type,
                        att.size_bytes, att.part_index, ge.gmail_id);
                }

                ++stats.indexed;
                ++done;
                if (progress_cb && done % 100 == 0) progress_cb(done, stats.total);

            } catch (const std::exception& e) {
                std::cerr << "[Indexer] insertGmailBatch error for " << ge.gmail_id
                          << ": " << e.what() << "\n";
                ++stats.errors;
                ++done;
            }
        }

        ins.commit();
        if (progress_cb) progress_cb(done, stats.total);
    }

    return stats;
}
