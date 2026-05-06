#pragma once
#include <string>
#include <mutex>

// Loads and auto-refreshes OAuth2 tokens from a gyb-format cfg file.
// gyb cfg is standard OAuth2 JSON — client_id, client_secret, refresh_token, token, token_expiry.
class OAuthManager {
public:
    explicit OAuthManager(const std::string& cfg_path);

    // Returns a valid access token, refreshing if expired. Thread-safe.
    std::string accessToken();

private:
    void loadFromFile();
    bool isExpired() const;
    void refresh();
    void saveToFile();

    std::string cfg_path_;
    std::string client_id_;
    std::string client_secret_;
    std::string refresh_token_;
    std::string access_token_;
    std::string token_expiry_;   // ISO8601 e.g. "2026-05-05T03:18:40Z"
    std::string token_uri_;

    mutable std::mutex mu_;
};
