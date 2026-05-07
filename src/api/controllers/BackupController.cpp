#include "BackupController.h"
#include <json/json.h>

BackupManager* BackupController::backup_ = nullptr;

static drogon::HttpResponsePtr makeJsonResponse(const Json::Value& body,
                                                 drogon::HttpStatusCode code = drogon::k200OK) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(Json::writeString(builder, body));
    return resp;
}

static std::string stateStr(BackupState s) {
    switch (s) {
        case BackupState::IDLE:            return "idle";
        case BackupState::BACKUP_RUNNING:  return "backup_running";
        case BackupState::PURGE_RUNNING:   return "purge_running";
        case BackupState::INDEXING:        return "indexing";
        case BackupState::EMBEDDING:       return "embedding";
        case BackupState::ERROR:           return "error";
    }
    return "unknown";
}

void BackupController::getStatus(const drogon::HttpRequestPtr&,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!backup_) { cb(makeJsonResponse(Json::Value("not initialized"), drogon::k503ServiceUnavailable)); return; }

    auto s = backup_->status();
    Json::Value body;
    body["state"]         = stateStr(s.state);
    body["downloaded"]    = s.downloaded;
    body["skipped"]       = s.skipped;
    body["fetched"]       = s.fetched;
    body["total"]         = s.total;
    body["purged"]        = s.purged;
    body["current_batch"] = s.current_batch;
    body["total_batches"] = s.total_batches;
    body["embed_done"]    = s.embed_done;
    body["embed_errors"]  = s.embed_errors;
    body["embed_total"]   = s.embed_total;
    body["db_total"]      = s.db_total;
    body["db_embedded"]   = s.db_embedded;
    body["db_failed"]     = s.db_failed;
    body["last_run"]      = s.last_run;
    body["next_run"]      = s.next_run;
    if (!s.last_error.empty()) body["last_error"] = s.last_error;
    cb(makeJsonResponse(body));
}

void BackupController::postStart(const drogon::HttpRequestPtr& req,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!backup_) { cb(makeJsonResponse(Json::Value("not initialized"), drogon::k503ServiceUnavailable)); return; }

    BackupRunOptions opts;
    auto body = req->getJsonObject();
    if (body) {
        if (body->isMember("sync_query") && (*body)["sync_query"].isString())
            opts.sync_query = (*body)["sync_query"].asString();
        if (body->isMember("after_date") && (*body)["after_date"].isString())
            opts.after_date = (*body)["after_date"].asString();
        if (body->isMember("before_date") && (*body)["before_date"].isString())
            opts.before_date = (*body)["before_date"].asString();
        if (body->isMember("max_messages") && (*body)["max_messages"].isInt())
            opts.max_messages = (*body)["max_messages"].asInt();
        if (body->isMember("skip_sync") && (*body)["skip_sync"].isBool())
            opts.skip_sync = (*body)["skip_sync"].asBool();
        if (body->isMember("run_purge") && (*body)["run_purge"].isBool())
            opts.run_purge = (*body)["run_purge"].asBool();
        if (body->isMember("purge_only_embedded") && (*body)["purge_only_embedded"].isBool())
            opts.purge_only_embedded = (*body)["purge_only_embedded"].asBool();
        if (body->isMember("run_embedding") && (*body)["run_embedding"].isBool())
            opts.run_embedding = (*body)["run_embedding"].asBool();
    }

    if (backup_->start(opts)) {
        Json::Value resp; resp["started"] = true;
        cb(makeJsonResponse(resp));
    } else {
        Json::Value resp; resp["error"] = "already running";
        cb(makeJsonResponse(resp, drogon::k409Conflict));
    }
}

void BackupController::postStop(const drogon::HttpRequestPtr&,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    if (!backup_) { cb(makeJsonResponse(Json::Value("not initialized"), drogon::k503ServiceUnavailable)); return; }

    backup_->stop();
    Json::Value body; body["stopped"] = true;
    cb(makeJsonResponse(body));
}
