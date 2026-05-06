#pragma once
#include <string>
#include <functional>
#include <mutex>
#include "config/AppConfig.h"

enum class BackupState { IDLE, BACKUP_RUNNING, PURGE_RUNNING, INDEXING, EMBEDDING, ERROR };

struct BackupStatus {
    BackupState state       = BackupState::IDLE;
    int         downloaded  = 0;
    int         total       = 0;
    int         purged      = 0;
    std::string last_error;
    std::string last_run;   // ISO8601
    std::string next_run;   // ISO8601
};

class BackupManager {
public:
    using MqttPublisher = std::function<void(const std::string& topic, const std::string& payload)>;

    explicit BackupManager(const AppConfig& cfg, MqttPublisher publisher);

    bool  start();   // returns false if already running
    void  stop();
    BackupStatus status() const;

private:
    void  runBackup();

    AppConfig     cfg_;
    MqttPublisher publish_;

    mutable std::mutex mu_;
    BackupStatus       status_;
};
