#pragma once
#include <string>
#include <vector>
#include <functional>
#include "config/AppConfig.h"
#include "mime/MimeParser.h"

struct IndexerStats {
    int total    = 0;
    int indexed  = 0;
    int skipped  = 0;  // already in DB
    int errors   = 0;
};

// Email ready to insert via the v1.5 Gmail API path
struct GmailEmail {
    ParsedEmail       parsed;
    std::string       gmail_id;
    std::string       thread_id;
    std::vector<std::string> labels;
};

class Indexer {
public:
    using ProgressCb = std::function<void(int done, int total)>;

    explicit Indexer(const AppConfig& cfg);

    // v1.0: walk gyb sqlite + .eml files → PostgreSQL
    IndexerStats run(const ProgressCb& progress_cb = nullptr);

    // v1.5: insert a batch of already-parsed emails directly (no sqlite, no .eml files)
    IndexerStats insertGmailBatch(const std::vector<GmailEmail>& emails,
                                  const ProgressCb& progress_cb = nullptr);

private:
    AppConfig cfg_;
};
