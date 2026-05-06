#include <gtest/gtest.h>
#include "search/SearchEngine.h"
#include "config/AppConfig.h"

static AppConfig badDbCfg() {
    AppConfig cfg;
    cfg.ollama_host        = "http://127.0.0.1:11434";
    cfg.embedding_batch_size = 5;
    cfg.db.host = "127.0.0.1";
    cfg.db.port = 19999;
    cfg.db.name = "nonexistent";
    cfg.db.user = "nobody";
    cfg.db.pass = "wrong";
    return cfg;
}

TEST(SearchEngine, ConstructsWithConfig) {
    EXPECT_NO_THROW(SearchEngine engine(badDbCfg()));
}

TEST(SearchEngine, FtsSearchThrowsOnBadConnection) {
    SearchEngine engine(badDbCfg());
    EXPECT_THROW(engine.search("test", 5, SearchMode::FTS), std::exception);
}

TEST(SearchEngine, GetEmailThrowsOnBadConnection) {
    SearchEngine engine(badDbCfg());
    EXPECT_THROW(engine.getEmail(1), std::exception);
}

TEST(SearchEngine, GetThreadThrowsOnBadConnection) {
    SearchEngine engine(badDbCfg());
    EXPECT_THROW(engine.getThread("thread-1", 10), std::exception);
}

TEST(SearchEngine, VectorSearchWithNoOllamaReturnsEmpty) {
    // Vector search calls Ollama first; with unreachable Ollama it should
    // return empty results rather than throwing.
    AppConfig cfg = badDbCfg();
    cfg.ollama_host = "http://127.0.0.1:19998"; // nothing listening
    SearchEngine engine(cfg);
    // embedQuery fails → returns {} → vectorSearch returns {} (no throw)
    EXPECT_NO_THROW({
        auto results = engine.search("test query", 5, SearchMode::VECTOR);
        EXPECT_TRUE(results.empty());
    });
}

TEST(SearchEngine, HybridSearchWithNoOllamaFallsBackToFts) {
    // Hybrid = FTS + vector RRF. With bad DB, FTS will throw.
    SearchEngine engine(badDbCfg());
    EXPECT_THROW(engine.search("test", 5, SearchMode::HYBRID), std::exception);
}
