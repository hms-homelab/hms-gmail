#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <csignal>
#include <string>

#include "config/AppConfig.h"
#include "db/Database.h"
#include "mqtt/MqttClient.h"
#include "backup/BackupManager.h"
#include "api/controllers/HealthController.h"
#include "api/controllers/BackupController.h"
#include "api/controllers/SearchController.h"
#include "search/SearchEngine.h"
#include "scheduler/CronScheduler.h"

static std::function<void()> shutdown_fn;

int main(int argc, char* argv[]) {
    std::string config_path = "/etc/hms-gmail/config.yaml";
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config") config_path = argv[i+1];
    }

    // 1. Config
    auto cfg = AppConfig::load(config_path);
    std::cout << "[hms-gmail] Starting v1.0.0 on port " << cfg.port << "\n";

    // 2. Database
    auto db = std::make_shared<Database>(cfg.db);
    db->ensureSchema();
    std::cout << "[DB] Schema ready on " << cfg.db.host << "/" << cfg.db.name << "\n";

    // 3. MQTT
    auto mqtt = std::make_shared<MqttClient>(cfg.mqtt);
    mqtt->connect();

    auto publish = [&mqtt](const std::string& topic, const std::string& payload) {
        mqtt->publish(topic, payload);
    };

    // 4. BackupManager
    auto backup = std::make_shared<BackupManager>(cfg, publish);

    // Subscribe to MQTT search requests
    mqtt->subscribe("gmail/search/request", [&cfg, &mqtt](const std::string&, const std::string& payload) {
        try {
            Json::Value req;
            Json::CharReaderBuilder rb;
            std::string errs;
            std::istringstream ss(payload);
            if (!Json::parseFromStream(rb, ss, &req, &errs)) return;
            std::string q = req.get("q", "").asString();
            if (q.empty()) return;
            int limit = req.get("limit", 10).asInt();
            SearchEngine engine(cfg);
            auto results = engine.search(q, limit);
            Json::Value arr(Json::arrayValue);
            for (const auto& r : results) {
                Json::Value v;
                v["id"]      = static_cast<Json::Int64>(r.id);
                v["subject"] = r.subject;
                v["from"]    = r.from_addr;
                v["date"]    = r.date;
                v["snippet"] = r.snippet;
                v["score"]   = r.score;
                arr.append(v);
            }
            Json::Value resp;
            resp["results"] = arr;
            resp["count"]   = static_cast<int>(results.size());
            Json::StreamWriterBuilder wb;
            wb["indentation"] = "";
            mqtt->publish("gmail/search/response", Json::writeString(wb, resp));
        } catch (const std::exception& e) {
            std::cerr << "[MQTT search] " << e.what() << "\n";
        }
    });

    // 5. Wire controllers
    BackupController::setBackupManager(backup.get());
    SearchController::setConfig(&cfg);

    // 6. Scheduler — auto-backup on cron schedule (e.g. "0 3 * * 0" = Sunday 3am UTC)
    std::unique_ptr<CronScheduler> scheduler;
    if (!cfg.schedule_cron.empty() && cfg.schedule_cron != "disabled") {
        scheduler = std::make_unique<CronScheduler>(cfg.schedule_cron, [&backup]() {
            std::cout << "[Scheduler] Triggering scheduled backup\n";
            backup->start();
        });
        std::cout << "[Scheduler] Cron schedule: " << cfg.schedule_cron << "\n";
    }

    // 7. Signal handlers
    shutdown_fn = [&scheduler]{ if (scheduler) scheduler->stop(); drogon::app().quit(); };
    std::signal(SIGINT,  [](int){ if (shutdown_fn) shutdown_fn(); });
    std::signal(SIGTERM, [](int){ if (shutdown_fn) shutdown_fn(); });

    // 7. Drogon — serve Angular frontend from dist/browser
    drogon::app()
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", cfg.port)
        .setThreadNum(4)
        .setDocumentRoot("/etc/hms-gmail/frontend")
        .setStaticFilesCacheTime(0)
        .run();

    std::cout << "[hms-gmail] Shutdown complete\n";
    return 0;
}
