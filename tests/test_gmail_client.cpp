#include <gtest/gtest.h>
#include "gmail/GmailClient.h"
#include "gmail/OAuthManager.h"
#include "scheduler/CronScheduler.h"
#include <fstream>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>

// ── OAuthManager helpers ─────────────────────────────────────────────────────

static const char* TMP_CFG = "/tmp/hms_gmail_test_gc.json";

static void writeCfg(const std::string& token, const std::string& expiry) {
    std::ofstream f(TMP_CFG);
    f << "{\n"
      << "  \"client_id\": \"test_id\",\n"
      << "  \"client_secret\": \"test_secret\",\n"
      << "  \"refresh_token\": \"test_refresh\",\n"
      << "  \"token\": \"" << token << "\",\n"
      << "  \"token_expiry\": \"" << expiry << "\",\n"
      << "  \"token_uri\": \"https://oauth2.googleapis.com/token\"\n"
      << "}\n";
}

// ── GmailClient tests ────────────────────────────────────────────────────────

// GmailClient wraps an OAuthManager reference — when the token is valid and
// unexpired, the client constructs fine. API calls will fail with a network
// error (no real server in test env), not an auth error.

TEST(GmailClient, ConstructsWithValidOAuth) {
    writeCfg("valid_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    EXPECT_NO_THROW({ GmailClient client(oauth); });
    std::remove(TMP_CFG);
}

TEST(GmailClient, ListMessageIdsThrowsOnNetworkError) {
    writeCfg("valid_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    GmailClient client(oauth);
    // No real Gmail server — should throw runtime_error (curl or HTTP error)
    EXPECT_THROW(client.listMessageIds(""), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(GmailClient, ListHistoryThrowsOnNetworkError) {
    writeCfg("valid_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    GmailClient client(oauth);
    EXPECT_THROW(client.listHistory("12345"), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(GmailClient, GetHistoryIdThrowsOnNetworkError) {
    writeCfg("valid_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    GmailClient client(oauth);
    EXPECT_THROW(client.getHistoryId(), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(GmailClient, ApiCallsThrowWhenOAuthExpired) {
    writeCfg("expired_token", "2020-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    GmailClient client(oauth);
    // accessToken() triggers refresh, which fails (invalid credentials) — throws
    EXPECT_THROW(client.listMessageIds(""), std::runtime_error);
    std::remove(TMP_CFG);
}

TEST(GmailClient, BatchGetRawEmptyIdsReturnsEmpty) {
    writeCfg("valid_token", "2099-01-01T00:00:00Z");
    OAuthManager oauth(TMP_CFG);
    GmailClient client(oauth);
    // Empty IDs list — should return immediately without an API call
    auto result = client.batchGetRaw({});
    EXPECT_TRUE(result.empty());
    std::remove(TMP_CFG);
}

// ── CronScheduler tests ───────────────────────────────────────────────────────

TEST(CronScheduler, ParsesValidExpression) {
    // Should not throw for valid 5-field expressions
    std::atomic<int> fires{0};
    // "* * * * *" = every minute. We stop immediately, just testing parse.
    EXPECT_NO_THROW({
        CronScheduler sched("* * * * *", [&]{ ++fires; });
        sched.stop();
    });
}

TEST(CronScheduler, ThrowsOnInvalidExpression) {
    EXPECT_THROW({
        CronScheduler sched("not_valid", [](){});
    }, std::exception);
}

TEST(CronScheduler, ThrowsOnTooFewFields) {
    EXPECT_THROW({
        CronScheduler sched("0 3 *", [](){});
    }, std::exception);
}

TEST(CronScheduler, ParsesNumericFields) {
    std::atomic<int> fires{0};
    // Sunday 3am = "0 3 * * 0" — far in future, stop immediately
    EXPECT_NO_THROW({
        CronScheduler sched("0 3 * * 0", [&]{ ++fires; });
        sched.stop();
    });
    EXPECT_EQ(fires.load(), 0);
}

TEST(CronScheduler, StopPreventsCallback) {
    std::atomic<int> fires{0};
    {
        CronScheduler sched("* * * * *", [&]{ ++fires; });
        sched.stop();
    }
    // Give the thread a moment to confirm no spurious fire
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(fires.load(), 0);
}
