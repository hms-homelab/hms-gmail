#pragma once
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <ctime>
#include "config/AppConfig.h"

class MqttClient {
public:
    explicit MqttClient(const MqttConfig& cfg);
    ~MqttClient();

    void connect();
    void publish(const std::string& topic, const std::string& payload, bool retain = false);
    void subscribe(const std::string& topic, std::function<void(const std::string&, const std::string&)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MqttConfig cfg_;
};
