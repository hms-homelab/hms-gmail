#pragma once
#include <drogon/HttpController.h>
#include "config/AppConfig.h"

class SearchController : public drogon::HttpController<SearchController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SearchController::search,    "/search",        drogon::Get);
    ADD_METHOD_TO(SearchController::getEmail,  "/emails/{id}",   drogon::Get);
    ADD_METHOD_TO(SearchController::getThread, "/threads/{tid}", drogon::Get);
    METHOD_LIST_END

    void search(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);

    void getEmail(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                  long id);

    void getThread(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& cb,
                   const std::string& tid);

    static void setConfig(const AppConfig* cfg) { cfg_ = cfg; }

private:
    static const AppConfig* cfg_;
};
