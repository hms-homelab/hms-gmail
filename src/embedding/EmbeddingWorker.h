#pragma once
#include <functional>
#include <string>
#include <vector>
#include "config/AppConfig.h"

struct EmbedStats {
    int embedded = 0;
    int skipped  = 0;
    int errors   = 0;
};

class EmbeddingWorker {
public:
    using ProgressCb = std::function<void(int done, int total)>;

    explicit EmbeddingWorker(const AppConfig& cfg);

    EmbedStats run(const ProgressCb& on_progress = nullptr);

private:
    std::vector<float> fetchEmbedding(const std::string& text);

    AppConfig cfg_;
};
