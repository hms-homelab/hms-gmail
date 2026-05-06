#include "mqtt/MqttClient.h"
#include <mqtt/async_client.h>
#include <iostream>

// Internal impl using Paho directly (same pattern as hms-nvr)
struct MqttClient::Impl {
    std::unique_ptr<mqtt::async_client> client;
    bool connected = false;
    std::recursive_mutex mu;
};

MqttClient::MqttClient(const MqttConfig& cfg) : cfg_(cfg) {
    impl_ = std::make_unique<Impl>();
}

MqttClient::~MqttClient() {
    if (impl_->client && impl_->connected) {
        try { impl_->client->disconnect()->wait(); } catch (...) {}
    }
}

void MqttClient::connect() {
    std::string client_id = "hms-gmail-" + std::to_string(std::time(nullptr));
    impl_->client = std::make_unique<mqtt::async_client>(
        "tcp://" + cfg_.host + ":" + std::to_string(cfg_.port), client_id);

    impl_->client->set_connection_lost_handler([this](const std::string& cause) {
        std::lock_guard<std::recursive_mutex> lock(impl_->mu);
        impl_->connected = false;
        std::cerr << "[MQTT] Connection lost: " << cause << "\n";
    });
    impl_->client->set_connected_handler([this](const std::string&) {
        std::lock_guard<std::recursive_mutex> lock(impl_->mu);
        impl_->connected = true;
        std::cout << "[MQTT] Reconnected\n";
    });

    mqtt::connect_options opts;
    opts.set_keep_alive_interval(60);
    opts.set_clean_session(true);
    opts.set_user_name(cfg_.user);
    opts.set_password(cfg_.pass);
    opts.set_automatic_reconnect(1, 64);

    try {
        impl_->client->connect(opts)->wait();
        impl_->connected = true;
        std::cout << "[MQTT] Connected to " << cfg_.host << ":" << cfg_.port << "\n";
    } catch (const mqtt::exception& e) {
        std::cerr << "[MQTT] Connect failed: " << e.what() << "\n";
    }
}

void MqttClient::publish(const std::string& topic, const std::string& payload, bool retain) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    if (!impl_->connected || !impl_->client) return;
    try {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(1);
        msg->set_retained(retain);
        impl_->client->publish(msg);
    } catch (const mqtt::exception& e) {
        std::cerr << "[MQTT] Publish failed on " << topic << ": " << e.what() << "\n";
    }
}

void MqttClient::subscribe(const std::string& topic,
                            std::function<void(const std::string&, const std::string&)> cb) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    if (!impl_->connected || !impl_->client) return;
    impl_->client->set_message_callback([cb](mqtt::const_message_ptr msg) {
        cb(msg->get_topic(), msg->get_payload_str());
    });
    impl_->client->subscribe(topic, 1)->wait();
}
