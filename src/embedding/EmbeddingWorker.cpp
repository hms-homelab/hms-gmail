#include "EmbeddingWorker.h"
#include <pqxx/pqxx>
#include <curl/curl.h>
#include <json/json.h>
#include <iostream>
#include <sstream>

namespace {

size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Decode common HTML entities
std::string decodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string ent = s.substr(i + 1, semi - i - 1);
                if      (ent == "amp")  { out += '&';  i = semi + 1; continue; }
                else if (ent == "lt")   { out += '<';  i = semi + 1; continue; }
                else if (ent == "gt")   { out += '>';  i = semi + 1; continue; }
                else if (ent == "quot") { out += '"';  i = semi + 1; continue; }
                else if (ent == "apos") { out += '\''; i = semi + 1; continue; }
                else if (ent == "nbsp") { out += ' ';  i = semi + 1; continue; }
                else if (!ent.empty() && ent[0] == '#') {
                    // numeric entity — just skip
                    out += ' '; i = semi + 1; continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

// Strip URLs (http/https) — they're token-dense and semantically useless for embedding
std::string stripUrls(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        // Look for http:// or https://
        if ((s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0)) {
            // Skip until whitespace or end
            while (i < s.size() && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' &&
                   s[i] != '\t' && s[i] != '<' && s[i] != '"' && s[i] != '\'')
                ++i;
            out += ' ';
        } else {
            out += s[i++];
        }
    }
    return out;
}

// Strip HTML tags, decode entities, collapse whitespace
std::string stripHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (unsigned char c : s) {
        if (c == '<')  { in_tag = true;  continue; }
        if (c == '>')  { in_tag = false; out += ' '; continue; }
        if (in_tag)    continue;
        if (c == '\r' || c == '\n' || c == '\t') { out += ' '; continue; }
        out += static_cast<char>(c);
    }
    // Collapse runs of spaces
    std::string collapsed;
    collapsed.reserve(out.size());
    bool prev_space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!prev_space) collapsed += ' ';
            prev_space = true;
        } else {
            collapsed += c;
            prev_space = false;
        }
    }
    return decodeEntities(collapsed);
}

std::string truncate(const std::string& s, size_t max_bytes = 4000) {
    if (s.size() <= max_bytes) return s;
    // Walk back to a valid UTF-8 character boundary
    size_t pos = max_bytes;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return s.substr(0, pos);
}

std::string vecToLiteral(const std::vector<float>& v) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << ",";
        ss << v[i];
    }
    ss << "]";
    return ss.str();
}

} // namespace

EmbeddingWorker::EmbeddingWorker(const AppConfig& cfg) : cfg_(cfg) {}

std::string EmbeddingWorker::connStr() const {
    return "host="     + cfg_.db.host +
           " port="    + std::to_string(cfg_.db.port) +
           " dbname="  + cfg_.db.name +
           " user="    + cfg_.db.user +
           " password="+ cfg_.db.pass;
}

std::vector<float> EmbeddingWorker::fetchEmbedding(const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string url = cfg_.ollama_host + "/api/embed";

    Json::Value req;
    req["model"] = "nomic-embed-text";
    req["input"] = truncate(text);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string body = Json::writeString(wb, req);

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        std::cerr << "[EmbeddingWorker] curl error: " << curl_easy_strerror(rc) << "\n";
        return {};
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(response);
    if (!Json::parseFromStream(rb, ss, &root, &errs)) {
        std::cerr << "[EmbeddingWorker] JSON parse error: " << errs << "\n";
        return {};
    }

    const Json::Value* arr = nullptr;
    if (root.isMember("embeddings") && root["embeddings"].isArray() &&
        root["embeddings"].size() > 0 && root["embeddings"][0].isArray()) {
        arr = &root["embeddings"][0];
    } else if (root.isMember("embedding") && root["embedding"].isArray()) {
        arr = &root["embedding"];
    }
    if (!arr) return {};

    std::vector<float> vec;
    vec.reserve(arr->size());
    for (const auto& v : *arr)
        vec.push_back(v.asFloat());
    return vec;
}

EmbedStats EmbeddingWorker::run(const ProgressCb& on_progress, const StopFn& should_stop) {
    const std::string cs       = connStr();
    const int max_attempts     = cfg_.embedding_max_attempts;

    pqxx::connection pg(cs);

    // Crash recovery: reset any in_progress → pending
    {
        pqxx::work txn(pg);
        txn.exec("UPDATE emails SET embed_status='pending' WHERE embed_status='in_progress'");
        txn.commit();
    }

    // Count total pending for progress reporting
    int total = 0;
    {
        pqxx::work txn(pg);
        auto r = txn.exec(
            "SELECT COUNT(*) FROM emails WHERE embed_status='pending' AND embed_attempts < " +
            std::to_string(max_attempts));
        total = r[0][0].as<int>();
        txn.commit();
    }

    if (total == 0) return {};

    EmbedStats stats;
    int done = 0;

    while (true) {
        if (should_stop && should_stop()) break;

        // Claim one email
        long id = -1;
        std::string subj, from, body;
        {
            pqxx::work txn(pg);
            auto r = txn.exec(
                "UPDATE emails SET embed_status='in_progress' "
                "WHERE id = ("
                "  SELECT id FROM emails "
                "  WHERE embed_status='pending' AND embed_attempts < " + std::to_string(max_attempts) +
                "  ORDER BY id LIMIT 1 FOR UPDATE SKIP LOCKED"
                ") RETURNING id, subject, from_addr, body_text");
            txn.commit();
            if (r.empty()) break;
            id   = r[0][0].as<long>();
            subj = r[0][1].is_null() ? "" : r[0][1].as<std::string>();
            from = r[0][2].is_null() ? "" : r[0][2].as<std::string>();
            body = r[0][3].is_null() ? "" : r[0][3].as<std::string>();
        }

        // Fetch attachment filenames for this email
        std::string attachments;
        {
            pqxx::work txn(pg);
            auto r = txn.exec(
                "SELECT string_agg(filename, ' ') FROM email_attachments "
                "WHERE email_id=" + std::to_string(id) + " AND filename IS NOT NULL");
            txn.commit();
            if (!r.empty() && !r[0][0].is_null())
                attachments = r[0][0].as<std::string>();
        }

        std::string clean_subj = decodeEntities(subj);
        std::string clean_body = truncate(stripUrls(stripHtml(body)));
        std::string text       = clean_subj + "\n" + from + "\n" + attachments + "\n" + clean_body;
        bool        no_text    = text.find_first_not_of(" \t\r\n") == std::string::npos;

        bool success = false;
        if (no_text) {
            pqxx::work upd(pg);
            upd.exec("UPDATE emails SET embed_status='done' WHERE id=" + std::to_string(id));
            upd.commit();
            success = true;
        } else {
            auto vec = fetchEmbedding(text);
            if (!vec.empty()) {
                try {
                    std::string literal = vecToLiteral(vec);
                    pqxx::work upd(pg);
                    upd.exec_params(
                        "UPDATE emails SET embedding=$1::vector, embed_status='done' WHERE id=$2",
                        literal, id);
                    upd.commit();
                    ++stats.embedded;
                    success = true;
                } catch (const std::exception& e) {
                    std::cerr << "[EmbeddingWorker] DB write failed id=" << id
                              << ": " << e.what() << "\n";
                }
            }
        }

        if (!success) {
            pqxx::work upd(pg);
            upd.exec(
                "UPDATE emails SET "
                "  embed_status = CASE WHEN embed_attempts + 1 >= " + std::to_string(max_attempts) +
                "                 THEN 'failed' ELSE 'pending' END,"
                "  embed_attempts = embed_attempts + 1 "
                "WHERE id=" + std::to_string(id));
            upd.commit();
            ++stats.errors;
        }

        ++done;
        if (on_progress) on_progress(done, stats.errors, total);
    }

    return stats;
}
