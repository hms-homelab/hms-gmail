#pragma once
#include <string>
#include <vector>
#include "config/AppConfig.h"

enum class SearchMode { FTS, VECTOR, HYBRID };

struct SearchResult {
    long        id;
    std::string message_id;
    std::string thread_id;
    std::string from_addr;
    std::string subject;
    std::string date;    // ISO8601 string
    std::string snippet; // first 200 chars of body_text
    bool        has_attachment;
    double      score;
};

struct EmailDetail {
    long                     id;
    std::string              message_id;
    std::string              thread_id;
    std::string              from_addr;
    std::vector<std::string> to_addrs;
    std::vector<std::string> cc;
    std::string              subject;
    std::string              date;
    std::string              body_text;
    std::string              body_html;
    bool                     has_attachment;
    std::vector<std::string> labels;
};

class SearchEngine {
public:
    explicit SearchEngine(const AppConfig& cfg);

    std::vector<SearchResult> search(const std::string& query,
                                     int limit = 20,
                                     SearchMode mode = SearchMode::HYBRID);

    EmailDetail         getEmail(long id);
    std::vector<SearchResult> getThread(const std::string& thread_id, int limit = 50);

private:
    std::vector<float>        embedQuery(const std::string& query);
    std::vector<SearchResult> ftsSearch(const std::string& query, int limit);
    std::vector<SearchResult> vectorSearch(const std::string& query, int limit);
    std::vector<SearchResult> hybridSearch(const std::string& query, int limit);

    AppConfig cfg_;
};
