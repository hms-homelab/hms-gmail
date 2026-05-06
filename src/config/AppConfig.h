#pragma once
#include <string>

struct MqttConfig {
    std::string host     = "localhost";
    int         port     = 1883;
    std::string user     = "";
    std::string pass     = "";
};

struct DbConfig {
    std::string host = "localhost";
    int         port = 5432;
    std::string name = "gmail";
    std::string user = "gmail_user";
    std::string pass = "changeme";

    std::string connString() const {
        return "host=" + host + " port=" + std::to_string(port) +
               " dbname=" + name + " user=" + user + " password=" + pass;
    }
};

struct AppConfig {
    int         port                  = 8890;
    std::string email                 = "";
    int         purge_older_than_days = 30;
    std::string schedule_cron         = "0 3 * * 0";
    int         embedding_batch_size  = 20;
    std::string ollama_host           = "http://localhost:11434";

    // v1.5: direct Gmail API (replaces gyb)
    std::string gmail_oauth_file      = "/etc/hms-gmail/oauth.json";
    std::string gmail_sync_query      = "";   // empty = all mail
    int         gmail_batch_size      = 100;  // messages per batch API call (max 100)

    // legacy gyb fields (no longer used in v1.5 — kept for backward compat)
    std::string gyb_path              = "/opt/gyb/gyb";
    std::string gyb_config_folder     = "/opt/gyb";
    std::string backup_dir            = "/var/lib/hms-gmail/backup";

    MqttConfig  mqtt;
    DbConfig    db;

    static AppConfig load(const std::string& path);
    void applyEnvFallbacks();
};
