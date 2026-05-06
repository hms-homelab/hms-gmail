#include <gtest/gtest.h>
#include "gmail/OAuthManager.h"
#include <fstream>
#include <cstdio>

static const char* TMP_CFG = "/tmp/hms_gmail_test_oauth.json";

// Write a minimal gyb-format cfg with a non-expired token
static void writeCfg(const char* path, const std::string& token,
                     const std::string& expiry,
                     const std::string& refresh = "test_refresh",
                     const std::string& client_id = "test_client_id",
                     const std::string& client_secret = "test_secret") {
    std::ofstream f(path);
    f << "{\n"
      << "  \"client_id\": \"" << client_id << "\",\n"
      << "  \"client_secret\": \"" << client_secret << "\",\n"
      << "  \"refresh_token\": \"" << refresh << "\",\n"
      << "  \"token\": \"" << token << "\",\n"
      << "  \"token_expiry\": \"" << expiry << "\",\n"
      << "  \"token_uri\": \"https://oauth2.googleapis.com/token\"\n"
      << "}\n";
}

TEST(OAuthManager, ThrowsOnMissingFile) {
    EXPECT_THROW({ OAuthManager o("/nonexistent/path.json"); }, std::runtime_error);
}

TEST(OAuthManager, ThrowsOnInvalidJson) {
    std::ofstream f(TMP_CFG);
    f << "not json at all {{{";
    f.close();
    EXPECT_THROW({ OAuthManager o(TMP_CFG); }, std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(OAuthManager, LoadsValidToken) {
    // Token expiry far in the future — no refresh needed
    writeCfg(TMP_CFG, "valid_access_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    EXPECT_EQ(oauth.accessToken(), "valid_access_token");
    std::remove(TMP_CFG);
}

TEST(OAuthManager, ExpiredTokenTriggersRefresh) {
    // Token expired in the past — accessToken() should attempt refresh and throw
    // (no real network in test env — expect the runtime_error from curl or HTTP)
    writeCfg(TMP_CFG, "old_token", "2020-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    // Refresh will fail (no network / invalid credentials) — just verify it throws
    // rather than silently returning an expired token
    EXPECT_THROW(oauth.accessToken(), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(OAuthManager, EmptyExpiryTriggersRefresh) {
    writeCfg(TMP_CFG, "old_token", "");
    OAuthManager oauth(TMP_CFG);
    EXPECT_THROW(oauth.accessToken(), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(OAuthManager, EmptyAccessTokenTriggersRefresh) {
    writeCfg(TMP_CFG, "", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    // Empty token — should attempt refresh, which fails without real credentials
    EXPECT_THROW(oauth.accessToken(), std::runtime_error);
    std::remove(TMP_CFG);
}
