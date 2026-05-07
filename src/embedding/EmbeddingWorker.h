#pragma once
#include <functional>
#include <string>
#include <vector>
#include "config/AppConfig.h"

struct EmbedStats {
    int embedded = 0;
    int skipped  = 0;
    int errors   = 0;
    int failed   = 0;  // permanently failed (hit max_attempts)
};

class EmbeddingWorker {
public:
    using ProgressCb = std::function<void(int done, int errors, int total)>;
    using StopFn     = std::function<bool()>;  // returns true when stop requested

    explicit EmbeddingWorker(const AppConfig& cfg);

    EmbedStats run(const ProgressCb& on_progress = nullptr, const StopFn& should_stop = nullptr);

private:
    std::vector<float> fetchEmbedding(const std::string& text);
    std::string connStr() const;

    AppConfig cfg_;
};
