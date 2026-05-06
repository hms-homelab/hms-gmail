#pragma once
#include "OAuthManager.h"
#include <string>
#include <vector>
#include <functional>

struct RawMessage {
    std::string id;
    std::string thread_id;
    std::string raw_rfc822;   // base64url-encoded RFC822, decode to get .eml bytes
    std::vector<std::string> label_ids;
};

struct HistoryDelta {
    std::vector<std::string> added_ids;
    std::vector<std::string> deleted_ids;
    std::string new_history_id;
};

struct AttachmentData {
    std::string attachment_id;
    std::string data;   // base64url-encoded bytes
    int size_bytes = 0;
};

// Gmail API client using libcurl. All methods throw std::runtime_error on API/network errors.
class GmailClient {
public:
    explicit GmailClient(OAuthManager& oauth);

    // List all message IDs matching a query. Paginates automatically.
    // progress_cb called with (fetched_so_far) after each page.
    std::vector<std::string> listMessageIds(
        const std::string& query,
        std::function<void(int)> progress_cb = nullptr);

    // Batch fetch up to 100 messages (format=raw, RFC822).
    // Returns vector in same order as input ids (missing ones omitted).
    std::vector<RawMessage> batchGetRaw(const std::vector<std::string>& ids);

    // Fetch incremental changes since startHistoryId.
    HistoryDelta listHistory(const std::string& start_history_id);

    // Move message to trash.
    void trashMessage(const std::string& id);

    // Fetch attachment bytes by message + attachment ID.
    AttachmentData getAttachment(const std::string& message_id,
                                 const std::string& attachment_id);

    // Returns the current historyId from the user's Gmail profile.
    std::string getHistoryId();

private:
    std::string get(const std::string& url);
    std::string post(const std::string& url, const std::string& body,
                     const std::string& content_type);

    OAuthManager& oauth_;

    static constexpr const char* kBaseUrl = "https://gmail.googleapis.com/gmail/v1/users/me";
    static constexpr const char* kBatchUrl = "https://www.googleapis.com/batch/gmail/v1";
};
