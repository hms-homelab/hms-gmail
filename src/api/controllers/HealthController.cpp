#include "HealthController.h"
#include <json/json.h>

void HealthController::health(const drogon::HttpRequestPtr&,
                              std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    Json::Value body;
    body["status"]  = "ok";
    body["service"] = "hms-gmail";
    body["version"] = version();

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(Json::writeString(builder, body));
    cb(resp);
}
