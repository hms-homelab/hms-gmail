#include <gtest/gtest.h>
#include "embedding/EmbeddingWorker.h"
#include "config/AppConfig.h"

// Unit tests for EmbeddingWorker that don't require a live Ollama or PostgreSQL.
// Integration (live) tests are handled manually via the API.

TEST(EmbeddingWorker, ConstructsWithConfig) {
    AppConfig cfg;
    cfg.ollama_host = "http://127.0.0.1:11434";
    cfg.embedding_batch_size = 10;
    cfg.db.host = "localhost";
    cfg.db.port = 5432;
    cfg.db.name = "gmail";
    cfg.db.user = "maestro";
    cfg.db.pass = "maestro_postgres_2026_secure";
    // Just verify it constructs without throwing
    EXPECT_NO_THROW(EmbeddingWorker worker(cfg));
}

TEST(EmbeddingWorker, RunWithNoDBConnectionReturnsEmptyStats) {
    AppConfig cfg;
    cfg.ollama_host = "http://127.0.0.1:11434";
    cfg.embedding_batch_size = 5;
    cfg.db.host = "127.0.0.1";
    cfg.db.port = 19999; // nothing listening
    cfg.db.name = "nonexistent";
    cfg.db.user = "nobody";
    cfg.db.pass = "wrong";

    EmbeddingWorker worker(cfg);
    // Should throw pqxx exception — not crash
    EXPECT_THROW(worker.run(), std::exception);
}
