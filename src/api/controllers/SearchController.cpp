#include "SearchController.h"
#include "search/SearchEngine.h"
#include <json/json.h>
#include <stdexcept>

const AppConfig* SearchController::cfg_ = nullptr;

namespace {

Json::Value resultToJson(const SearchResult& r) {
    Json::Value v;
    v["id"]             = static_cast<Json::Int64>(r.id);
    v["message_id"]     = r.message_id;
    v["thread_id"]      = r.thread_id;
    v["from"]           = r.from_addr;
    v["subject"]        = r.subject;
    v["date"]           = r.date;
    v["snippet"]        = r.snippet;
    v["has_attachment"] = r.has_attachment;
    v["score"]          = r.score;
    return v;
}

drogon::HttpResponsePtr jsonResp(const Json::Value& body,
                                 drogon::HttpStatusCode code = drogon::k200OK) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(Json::writeString(wb, body));
    return resp;
}

drogon::HttpResponsePtr errorResp(const std::string& msg, drogon::HttpStatusCode code) {
    Json::Value v;
    v["error"] = msg;
    return jsonResp(v, code);
}

} // namespace

void SearchController::search(const drogon::HttpRequestPtr& req,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!cfg_) { cb(errorResp("not initialized", drogon::k500InternalServerError)); return; }

    std::string query = req->getParameter("q");
    if (query.empty()) {
        cb(errorResp("missing query parameter 'q'", drogon::k400BadRequest));
        return;
    }

    int limit = 20;
    auto lim_str = req->getParameter("limit");
    if (!lim_str.empty()) {
        try { limit = std::stoi(lim_str); } catch (...) {}
        limit = std::max(1, std::min(limit, 100));
    }

    SearchMode mode = SearchMode::HYBRID;
    auto mode_str = req->getParameter("mode");
    if (mode_str == "fts")         mode = SearchMode::FTS;
    else if (mode_str == "vector") mode = SearchMode::VECTOR;

    AppConfig cfg = *cfg_;
    drogon::app().getLoop()->runInLoop([cfg, query, limit, mode, cb = std::move(cb)]() mutable {
        try {
            SearchEngine engine(cfg);
            auto results = engine.search(query, limit, mode);
            Json::Value arr(Json::arrayValue);
            for (const auto& r : results)
                arr.append(resultToJson(r));
            Json::Value body;
            body["results"] = arr;
            body["count"]   = static_cast<int>(results.size());
            body["query"]   = query;
            cb(jsonResp(body));
        } catch (const std::exception& e) {
            cb(errorResp(e.what(), drogon::k500InternalServerError));
        }
    });
}

void SearchController::getEmail(const drogon::HttpRequestPtr& req,
                                std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                long id) {
    (void)req;
    if (!cfg_) { cb(errorResp("not initialized", drogon::k500InternalServerError)); return; }

    AppConfig cfg = *cfg_;
    drogon::app().getLoop()->runInLoop([cfg, id, cb = std::move(cb)]() mutable {
        try {
            SearchEngine engine(cfg);
            auto d = engine.getEmail(id);
            Json::Value v;
            v["id"]             = static_cast<Json::Int64>(d.id);
            v["message_id"]     = d.message_id;
            v["thread_id"]      = d.thread_id;
            v["from"]           = d.from_addr;
            v["subject"]        = d.subject;
            v["date"]           = d.date;
            v["body_text"]      = d.body_text;
            v["body_html"]      = d.body_html;
            v["has_attachment"] = d.has_attachment;
            cb(jsonResp(v));
        } catch (const std::runtime_error& e) {
            cb(errorResp(e.what(), drogon::k404NotFound));
        } catch (const std::exception& e) {
            cb(errorResp(e.what(), drogon::k500InternalServerError));
        }
    });
}

void SearchController::getThread(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                                 const std::string& tid) {
    (void)req;
    if (!cfg_) { cb(errorResp("not initialized", drogon::k500InternalServerError)); return; }

    AppConfig cfg = *cfg_;
    drogon::app().getLoop()->runInLoop([cfg, tid, cb = std::move(cb)]() mutable {
        try {
            SearchEngine engine(cfg);
            auto results = engine.getThread(tid, 50);
            Json::Value arr(Json::arrayValue);
            for (const auto& r : results)
                arr.append(resultToJson(r));
            Json::Value body;
            body["thread_id"] = tid;
            body["emails"]    = arr;
            body["count"]     = static_cast<int>(results.size());
            cb(jsonResp(body));
        } catch (const std::exception& e) {
            cb(errorResp(e.what(), drogon::k500InternalServerError));
        }
    });
}
