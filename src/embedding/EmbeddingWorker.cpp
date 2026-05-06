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

// Truncate text to avoid blowing token limits (nomic-embed-text: ~8192 tokens)
std::string truncate(const std::string& s, size_t max_bytes = 16000) {
    if (s.size() <= max_bytes) return s;
    return s.substr(0, max_bytes);
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

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

    // Ollama /api/embed returns {"embeddings":[[...]]} (array of arrays)
    // Older versions used {"embedding":[...]}
    const Json::Value* arr = nullptr;
    if (root.isMember("embeddings") && root["embeddings"].isArray() &&
        root["embeddings"].size() > 0 && root["embeddings"][0].isArray()) {
        arr = &root["embeddings"][0];
    } else if (root.isMember("embedding") && root["embedding"].isArray()) {
        arr = &root["embedding"];
    }
    if (!arr) {
        std::cerr << "[EmbeddingWorker] unexpected response shape\n";
        return {};
    }

    std::vector<float> vec;
    vec.reserve(arr->size());
    for (const auto& v : *arr)
        vec.push_back(v.asFloat());
    return vec;
}

EmbedStats EmbeddingWorker::run(const ProgressCb& on_progress) {
    EmbedStats stats;

    std::string conn_str =
        "host="     + cfg_.db.host +
        " port="    + std::to_string(cfg_.db.port) +
        " dbname="  + cfg_.db.name +
        " user="    + cfg_.db.user +
        " password="+ cfg_.db.pass;

    pqxx::connection conn(conn_str);

    // Count how many need embedding
    int total = 0;
    {
        pqxx::work txn(conn);
        auto r = txn.exec("SELECT COUNT(*) FROM emails WHERE embedding IS NULL");
        total = r[0][0].as<int>();
        txn.commit();
    }

    if (total == 0) return stats;

    stats.skipped = 0;

    int done = 0;
    while (true) {
        pqxx::work txn(conn);
        auto rows = txn.exec(
            "SELECT id, subject, body_text FROM emails WHERE embedding IS NULL LIMIT " +
            std::to_string(cfg_.embedding_batch_size));
        txn.commit();

        if (rows.empty()) break;

        for (const auto& row : rows) {
            long id            = row[0].as<long>();
            std::string subj   = row[1].is_null() ? "" : row[1].as<std::string>();
            std::string body   = row[2].is_null() ? "" : row[2].as<std::string>();
            std::string text   = subj + "\n" + body;

            auto vec = fetchEmbedding(text);
            if (vec.empty()) {
                ++stats.errors;
                ++done;
                continue;
            }

            std::string literal = vecToLiteral(vec);
            try {
                pqxx::work upd(conn);
                upd.exec(
                    "UPDATE emails SET embedding = '" + literal +
                    "'::vector WHERE id = " + std::to_string(id));
                upd.commit();
                ++stats.embedded;
            } catch (const std::exception& e) {
                std::cerr << "[EmbeddingWorker] DB update failed for id=" << id
                          << ": " << e.what() << "\n";
                ++stats.errors;
            }

            ++done;
            if (on_progress) on_progress(done, total);
        }
    }

    return stats;
}
