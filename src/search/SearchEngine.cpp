#include "SearchEngine.h"
#include <pqxx/pqxx>
#include <curl/curl.h>
#include <json/json.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace {

size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, std::string* out) {
    out->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

std::string snippet(const std::string& body, size_t max = 200) {
    if (body.size() <= max) return body;
    auto s = body.substr(0, max);
    // trim to last word boundary
    auto pos = s.rfind(' ');
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s + "...";
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

pqxx::connection makeConn(const AppConfig& cfg) {
    return pqxx::connection(
        "host="     + cfg.db.host +
        " port="    + std::to_string(cfg.db.port) +
        " dbname="  + cfg.db.name +
        " user="    + cfg.db.user +
        " password="+ cfg.db.pass);
}

SearchResult rowToResult(const pqxx::row& r, double score) {
    SearchResult res;
    res.id             = r[0].as<long>();
    res.message_id     = r[1].is_null() ? "" : r[1].as<std::string>();
    res.thread_id      = r[2].is_null() ? "" : r[2].as<std::string>();
    res.from_addr      = r[3].is_null() ? "" : r[3].as<std::string>();
    res.subject        = r[4].is_null() ? "" : r[4].as<std::string>();
    res.date           = r[5].is_null() ? "" : r[5].as<std::string>();
    std::string body   = r[6].is_null() ? "" : r[6].as<std::string>();
    res.snippet        = snippet(body);
    res.has_attachment = r[7].is_null() ? false : r[7].as<bool>();
    res.score          = score;
    return res;
}

} // namespace

SearchEngine::SearchEngine(const AppConfig& cfg) : cfg_(cfg) {}

std::vector<float> SearchEngine::embedQuery(const std::string& query) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string url = cfg_.ollama_host + "/api/embed";
    Json::Value req;
    req["model"] = "nomic-embed-text";
    req["input"] = query.size() > 2000 ? query.substr(0, 2000) : query;
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) return {};

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(response);
    if (!Json::parseFromStream(rb, ss, &root, &errs)) return {};

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

std::vector<SearchResult> SearchEngine::ftsSearch(const std::string& query, int limit) {
    auto conn = makeConn(cfg_);
    pqxx::work txn(conn);
    auto rows = txn.exec(
        "SELECT id, message_id, thread_id, from_addr, subject, "
        "       to_char(date, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), body_text, has_attachment, "
        "       ts_rank(search_vec, plainto_tsquery('english', " + txn.quote(query) + ")) AS rank "
        "FROM emails "
        "WHERE search_vec @@ plainto_tsquery('english', " + txn.quote(query) + ") "
        "ORDER BY rank DESC "
        "LIMIT " + std::to_string(limit));
    txn.commit();

    std::vector<SearchResult> results;
    for (const auto& r : rows) {
        double score = r[8].is_null() ? 0.0 : r[8].as<double>();
        results.push_back(rowToResult(r, score));
    }
    return results;
}

std::vector<SearchResult> SearchEngine::vectorSearch(const std::string& query, int limit) {
    auto vec = embedQuery(query);
    if (vec.empty()) return {};

    std::string literal = vecToLiteral(vec);
    auto conn = makeConn(cfg_);
    pqxx::work txn(conn);
    auto rows = txn.exec(
        "SELECT id, message_id, thread_id, from_addr, subject, "
        "       to_char(date, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), body_text, has_attachment, "
        "       1 - (embedding <=> '" + literal + "'::vector) AS score "
        "FROM emails "
        "WHERE embedding IS NOT NULL "
        "ORDER BY embedding <=> '" + literal + "'::vector "
        "LIMIT " + std::to_string(limit));
    txn.commit();

    std::vector<SearchResult> results;
    for (const auto& r : rows) {
        double score = r[8].is_null() ? 0.0 : r[8].as<double>();
        results.push_back(rowToResult(r, score));
    }
    return results;
}

std::vector<SearchResult> SearchEngine::hybridSearch(const std::string& query, int limit) {
    // RRF: score = 1/(k + rank), k=60
    const double k = 60.0;

    auto fts_res = ftsSearch(query, limit * 2);
    auto vec_res = vectorSearch(query, limit * 2);

    std::unordered_map<long, double> rrf;
    std::unordered_map<long, SearchResult> by_id;

    for (size_t i = 0; i < fts_res.size(); ++i) {
        rrf[fts_res[i].id] += 1.0 / (k + static_cast<double>(i + 1));
        by_id[fts_res[i].id] = fts_res[i];
    }
    for (size_t i = 0; i < vec_res.size(); ++i) {
        rrf[vec_res[i].id] += 1.0 / (k + static_cast<double>(i + 1));
        by_id[vec_res[i].id] = vec_res[i];
    }

    std::vector<std::pair<long, double>> ranked(rrf.begin(), rrf.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    std::vector<SearchResult> results;
    results.reserve(std::min(limit, static_cast<int>(ranked.size())));
    for (int i = 0; i < limit && i < static_cast<int>(ranked.size()); ++i) {
        auto& res = by_id[ranked[i].first];
        res.score = ranked[i].second;
        results.push_back(res);
    }
    return results;
}

std::vector<SearchResult> SearchEngine::search(const std::string& query,
                                                int limit, SearchMode mode) {
    switch (mode) {
        case SearchMode::FTS:    return ftsSearch(query, limit);
        case SearchMode::VECTOR: return vectorSearch(query, limit);
        default:                 return hybridSearch(query, limit);
    }
}

EmailDetail SearchEngine::getEmail(long id) {
    auto conn = makeConn(cfg_);
    pqxx::work txn(conn);
    auto rows = txn.exec(
        "SELECT id, message_id, thread_id, from_addr, to_addrs::text, cc::text, "
        "       subject, to_char(date, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), "
        "       body_text, body_html, has_attachment, labels::text "
        "FROM emails WHERE id = " + std::to_string(id));
    txn.commit();

    if (rows.empty()) throw std::runtime_error("email not found: " + std::to_string(id));
    const auto& r = rows[0];

    EmailDetail d;
    d.id             = r[0].as<long>();
    d.message_id     = r[1].is_null() ? "" : r[1].as<std::string>();
    d.thread_id      = r[2].is_null() ? "" : r[2].as<std::string>();
    d.from_addr      = r[3].is_null() ? "" : r[3].as<std::string>();
    // to_addrs and cc are returned as PostgreSQL array literals — parse lightly
    d.subject        = r[6].is_null() ? "" : r[6].as<std::string>();
    d.date           = r[7].is_null() ? "" : r[7].as<std::string>();
    d.body_text      = r[8].is_null() ? "" : r[8].as<std::string>();
    d.body_html      = r[9].is_null() ? "" : r[9].as<std::string>();
    d.has_attachment = r[10].is_null() ? false : r[10].as<bool>();
    return d;
}

std::vector<SearchResult> SearchEngine::getThread(const std::string& thread_id, int limit) {
    auto conn = makeConn(cfg_);
    pqxx::work txn(conn);
    auto rows = txn.exec(
        "SELECT id, message_id, thread_id, from_addr, subject, "
        "       to_char(date, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), body_text, has_attachment "
        "FROM emails WHERE thread_id = " + txn.quote(thread_id) +
        " ORDER BY date ASC LIMIT " + std::to_string(limit));
    txn.commit();

    std::vector<SearchResult> results;
    for (const auto& r : rows) {
        SearchResult res;
        res.id             = r[0].as<long>();
        res.message_id     = r[1].is_null() ? "" : r[1].as<std::string>();
        res.thread_id      = r[2].is_null() ? "" : r[2].as<std::string>();
        res.from_addr      = r[3].is_null() ? "" : r[3].as<std::string>();
        res.subject        = r[4].is_null() ? "" : r[4].as<std::string>();
        res.date           = r[5].is_null() ? "" : r[5].as<std::string>();
        std::string body   = r[6].is_null() ? "" : r[6].as<std::string>();
        res.snippet        = snippet(body);
        res.has_attachment = r[7].is_null() ? false : r[7].as<bool>();
        res.score          = 0.0;
        results.push_back(res);
    }
    return results;
}
