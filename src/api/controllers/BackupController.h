#pragma once
#include <drogon/HttpController.h>
#include "backup/BackupManager.h"

class BackupController : public drogon::HttpController<BackupController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BackupController::getStatus, "/api/backup/status", drogon::Get);
        ADD_METHOD_TO(BackupController::postStart,  "/api/backup/start",  drogon::Post);
        ADD_METHOD_TO(BackupController::postStop,   "/api/backup/stop",   drogon::Post);
    METHOD_LIST_END

    void getStatus(const drogon::HttpRequestPtr&,
                   std::function<void(const drogon::HttpResponsePtr&)>&&);
    void postStart(const drogon::HttpRequestPtr&,
                   std::function<void(const drogon::HttpResponsePtr&)>&&);
    void postStop(const drogon::HttpRequestPtr&,
                  std::function<void(const drogon::HttpResponsePtr&)>&&);

    static void setBackupManager(BackupManager* bm) { backup_ = bm; }

private:
    static BackupManager* backup_;
};
