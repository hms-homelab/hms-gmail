#include "AppConfig.h"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <stdexcept>

AppConfig AppConfig::load(const std::string& path) {
    AppConfig cfg;
    try {
        YAML::Node y = YAML::LoadFile(path);
        if (y["port"])                  cfg.port                  = y["port"].as<int>();
        if (y["email"])                 cfg.email                 = y["email"].as<std::string>();
        if (y["purge_older_than_days"]) cfg.purge_older_than_days = y["purge_older_than_days"].as<int>();
        if (y["schedule_cron"])         cfg.schedule_cron         = y["schedule_cron"].as<std::string>();
        if (y["embedding_batch_size"])  cfg.embedding_batch_size  = y["embedding_batch_size"].as<int>();
        if (y["ollama_host"])           cfg.ollama_host           = y["ollama_host"].as<std::string>();
        if (y["gmail_oauth_file"])      cfg.gmail_oauth_file      = y["gmail_oauth_file"].as<std::string>();
        if (y["gmail_sync_query"])      cfg.gmail_sync_query      = y["gmail_sync_query"].as<std::string>();
        if (y["gmail_batch_size"])      cfg.gmail_batch_size      = y["gmail_batch_size"].as<int>();
        if (y["gyb_path"])              cfg.gyb_path              = y["gyb_path"].as<std::string>();
        if (y["gyb_config_folder"])     cfg.gyb_config_folder     = y["gyb_config_folder"].as<std::string>();
        if (y["backup_dir"])            cfg.backup_dir            = y["backup_dir"].as<std::string>();
        if (y["mqtt"]) {
            auto m = y["mqtt"];
            if (m["host"]) cfg.mqtt.host = m["host"].as<std::string>();
            if (m["port"]) cfg.mqtt.port = m["port"].as<int>();
            if (m["user"]) cfg.mqtt.user = m["user"].as<std::string>();
            if (m["pass"]) cfg.mqtt.pass = m["pass"].as<std::string>();
        }
        if (y["db"]) {
            auto d = y["db"];
            if (d["host"]) cfg.db.host = d["host"].as<std::string>();
            if (d["port"]) cfg.db.port = d["port"].as<int>();
            if (d["name"]) cfg.db.name = d["name"].as<std::string>();
            if (d["user"]) cfg.db.user = d["user"].as<std::string>();
            if (d["pass"]) cfg.db.pass = d["pass"].as<std::string>();
        }
    } catch (const YAML::BadFile&) {
        // no config file — use defaults
    }
    cfg.applyEnvFallbacks();
    return cfg;
}

void AppConfig::applyEnvFallbacks() {
    if (auto v = std::getenv("HMS_GMAIL_PORT"))        port        = std::stoi(v);
    if (auto v = std::getenv("HMS_GMAIL_EMAIL"))       email       = v;
    if (auto v = std::getenv("HMS_GMAIL_BACKUP_DIR"))  backup_dir  = v;
    if (auto v = std::getenv("HMS_GMAIL_OLLAMA_HOST")) ollama_host = v;
    if (auto v = std::getenv("MQTT_HOST"))             mqtt.host   = v;
    if (auto v = std::getenv("MQTT_USER"))             mqtt.user   = v;
    if (auto v = std::getenv("MQTT_PASS"))             mqtt.pass   = v;
    if (auto v = std::getenv("DB_HOST"))               db.host     = v;
    if (auto v = std::getenv("DB_NAME"))               db.name     = v;
    if (auto v = std::getenv("DB_USER"))               db.user     = v;
    if (auto v = std::getenv("DB_PASS"))               db.pass     = v;
}
