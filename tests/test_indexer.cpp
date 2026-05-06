#include <gtest/gtest.h>
#include "indexer/Indexer.h"
#include "config/AppConfig.h"
#include <sqlite3.h>
#include <fstream>
#include <cstdio>
#include <cstring>

// Creates a minimal gyb-compatible sqlite at path, returns 0 on success.
static int createTestSqlite(const char* path, int num_rows = 0) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path, &db) != SQLITE_OK) return 1;

    char* err = nullptr;
    sqlite3_exec(db,
        "CREATE TABLE messages ("
        "  message_num INTEGER PRIMARY KEY,"
        "  message_filename TEXT,"
        "  message_internaldate TEXT"
        ");",
        nullptr, nullptr, &err);
    sqlite3_free(err);

    for (int i = 0; i < num_rows; ++i) {
        std::string sql = "INSERT INTO messages VALUES (" +
            std::to_string(i) + ", 'nonexistent/file_" +
            std::to_string(i) + ".eml', '2026-05-04T00:00:00Z');";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
        sqlite3_free(err);
    }

    sqlite3_close(db);
    return 0;
}

// Minimal AppConfig pointing at temp dir, bad PG (to confirm early path)
static AppConfig testCfg(const std::string& backup_dir) {
    AppConfig cfg;
    cfg.backup_dir = backup_dir;
    cfg.db.host    = "127.0.0.1";
    cfg.db.port    = 19999; // nothing listening
    cfg.db.name    = "nonexistent";
    cfg.db.user    = "nobody";
    cfg.db.pass    = "wrong";
    return cfg;
}

TEST(Indexer, NonExistentBackupDirReturnsZeroStats) {
    AppConfig cfg = testCfg("/tmp/this_dir_does_not_exist_hms_gmail");
    Indexer idx(cfg);
    auto stats = idx.run();
    EXPECT_EQ(stats.total,   0);
    EXPECT_EQ(stats.indexed, 0);
    EXPECT_EQ(stats.errors,  0);
    EXPECT_EQ(stats.skipped, 0);
}

TEST(Indexer, EmptyDatabaseReturnsZeroStats) {
    const char* db_path  = "/tmp/hms_gmail_test_idx.sqlite";
    const char* test_dir = "/tmp/hms_gmail_test_dir";

    mkdir(test_dir, 0755);
    ASSERT_EQ(createTestSqlite(db_path, 0), 0);
    // Move sqlite to the expected location
    rename(db_path, (std::string(test_dir) + "/msg-db.sqlite").c_str());

    AppConfig cfg = testCfg(test_dir);
    Indexer idx(cfg);
    auto stats = idx.run();

    EXPECT_EQ(stats.total,   0);
    EXPECT_EQ(stats.indexed, 0);
    EXPECT_EQ(stats.errors,  0);

    std::remove((std::string(test_dir) + "/msg-db.sqlite").c_str());
    rmdir(test_dir);
}

TEST(Indexer, RowsWithBadPgConnectionThrows) {
    const char* test_dir = "/tmp/hms_gmail_test_dir2";
    mkdir(test_dir, 0755);

    std::string sqlite_path = std::string(test_dir) + "/msg-db.sqlite";
    ASSERT_EQ(createTestSqlite(sqlite_path.c_str(), 3), 0);

    // When rows exist, Indexer opens PG. With bad PG config it throws.
    AppConfig cfg = testCfg(test_dir);
    Indexer idx(cfg);
    EXPECT_THROW(idx.run(), std::exception);

    std::remove(sqlite_path.c_str());
    rmdir(test_dir);
}

TEST(Indexer, ProgressCallbackNotFiredOnEmptyDb) {
    const char* test_dir = "/tmp/hms_gmail_test_dir3";
    mkdir(test_dir, 0755);

    std::string sqlite_path = std::string(test_dir) + "/msg-db.sqlite";
    ASSERT_EQ(createTestSqlite(sqlite_path.c_str(), 0), 0);

    AppConfig cfg = testCfg(test_dir);
    Indexer idx(cfg);

    int cb_count = 0;
    // Empty DB → early return before PG connection → no throw, no callbacks
    EXPECT_NO_THROW(idx.run([&](int, int){ ++cb_count; }));
    EXPECT_EQ(cb_count, 0);

    std::remove(sqlite_path.c_str());
    rmdir(test_dir);
}
