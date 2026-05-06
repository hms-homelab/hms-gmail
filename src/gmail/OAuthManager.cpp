#include "OAuthManager.h"
#include <json/json.h>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <iostream>

namespace {

size_t curlWriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// Parse "2026-05-05T03:18:40Z" → time_t UTC
time_t parseIso8601(const std::string& s) {
    struct tm t{};
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%dZ",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return 0;
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    t.tm_isdst = 0;
#ifdef _WIN32
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
}

} // namespace

OAuthManager::OAuthManager(const std::string& cfg_path)
    : cfg_path_(cfg_path), token_uri_("https://oauth2.googleapis.com/token") {
    loadFromFile();
}

void OAuthManager::loadFromFile() {
    std::ifstream f(cfg_path_);
    if (!f) throw std::runtime_error("OAuthManager: cannot open " + cfg_path_);

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, f, &root, &errs))
        throw std::runtime_error("OAuthManager: JSON parse error: " + errs);

    client_id_      = root["client_id"].asString();
    client_secret_  = root["client_secret"].asString();
    refresh_token_  = root["refresh_token"].asString();
    access_token_   = root.get("token", "").asString();
    token_expiry_   = root.get("token_expiry", "").asString();
    if (root.isMember("token_uri"))
        token_uri_  = root["token_uri"].asString();
}

bool OAuthManager::isExpired() const {
    if (access_token_.empty()) return true;
    if (token_expiry_.empty()) return true;
    time_t expiry = parseIso8601(token_expiry_);
    if (expiry == 0) return true;
    // Refresh 60s before actual expiry
    return time(nullptr) >= (expiry - 60);
}

void OAuthManager::refresh() {
    std::string post_data =
        "client_id="     + client_id_ +
        "&client_secret=" + client_secret_ +
        "&refresh_token=" + refresh_token_ +
        "&grant_type=refresh_token";

    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("OAuthManager: curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, token_uri_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error("OAuthManager: curl error: " + std::string(curl_easy_strerror(rc)));
    if (http_code != 200)
        throw std::runtime_error("OAuthManager: token refresh HTTP " + std::to_string(http_code) + ": " + response);

    Json::Value resp;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(response);
    if (!Json::parseFromStream(rb, ss, &resp, &errs))
        throw std::runtime_error("OAuthManager: refresh response parse error: " + errs);

    if (!resp.isMember("access_token"))
        throw std::runtime_error("OAuthManager: no access_token in response: " + response);

    access_token_ = resp["access_token"].asString();

    // Compute expiry from expires_in (seconds)
    int expires_in = resp.get("expires_in", 3600).asInt();
    time_t expiry  = time(nullptr) + expires_in;
    struct tm t{};
    gmtime_r(&expiry, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    token_expiry_ = buf;

    saveToFile();
    std::cerr << "[OAuthManager] refreshed token, expires " << token_expiry_ << "\n";
}

void OAuthManager::saveToFile() {
    // Read existing file to preserve all fields (decoded_id_token, id_token, etc.)
    std::ifstream fin(cfg_path_);
    Json::Value root;
    if (fin) {
        Json::CharReaderBuilder rb;
        std::string errs;
        Json::parseFromStream(rb, fin, &root, &errs);
        fin.close();
    }

    root["token"]        = access_token_;
    root["token_expiry"] = token_expiry_;

    std::ofstream fout(cfg_path_);
    if (!fout) {
        std::cerr << "[OAuthManager] warning: cannot write " << cfg_path_ << "\n";
        return;
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    fout << Json::writeString(wb, root);
}

std::string OAuthManager::accessToken() {
    std::lock_guard<std::mutex> lock(mu_);
    if (isExpired()) refresh();
    return access_token_;
}
