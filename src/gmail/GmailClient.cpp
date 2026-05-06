#include "GmailClient.h"
#include <json/json.h>
#include <curl/curl.h>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <regex>

namespace {

size_t curlWriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// URL-encode a string (for query parameters)
std::string urlEncode(const std::string& s) {
    CURL* curl = curl_easy_init();
    if (!curl) return s;
    char* enc = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    std::string result = enc ? enc : s;
    if (enc) curl_free(enc);
    curl_easy_cleanup(curl);
    return result;
}

Json::Value parseJson(const std::string& body) {
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(body);
    if (!Json::parseFromStream(rb, ss, &root, &errs))
        throw std::runtime_error("GmailClient: JSON parse error: " + errs + "\nbody: " + body.substr(0, 200));
    return root;
}

// Parse multipart/mixed batch response body.
// Returns list of (status_line, body) pairs per sub-response.
std::vector<std::pair<int, std::string>> parseBatchResponse(
        const std::string& body, const std::string& boundary) {

    std::vector<std::pair<int, std::string>> parts;
    std::string delim = "--" + boundary;
    std::string end   = "--" + boundary + "--";

    size_t pos = 0;
    while ((pos = body.find(delim, pos)) != std::string::npos) {
        pos += delim.size();
        if (body.substr(pos, 2) == "--") break;  // end marker

        // Skip \r\n after delimiter
        while (pos < body.size() && (body[pos] == '\r' || body[pos] == '\n')) ++pos;

        // Find the inner body (after blank line separating outer headers from inner response)
        size_t inner_start = body.find("\r\n\r\n", pos);
        if (inner_start == std::string::npos) break;
        inner_start += 4;

        // Find the next boundary
        size_t next_delim = body.find(delim, inner_start);
        std::string part = body.substr(inner_start, next_delim - inner_start);

        // part is an HTTP response: "HTTP/1.1 200 OK\r\n...\r\n\r\n{json}"
        size_t header_end = part.find("\r\n\r\n");
        int status = 0;
        if (part.rfind("HTTP/1.1 ", 0) == 0 || part.rfind("HTTP/2 ", 0) == 0) {
            sscanf(part.c_str() + (part[4] == '/' ? 9 : 7), "%d", &status);
        } else {
            // Try finding status in inner headers
            sscanf(part.c_str(), "HTTP/%*s %d", &status);
        }
        std::string json_body = (header_end != std::string::npos)
            ? part.substr(header_end + 4)
            : part;
        // Trim trailing \r\n
        while (!json_body.empty() && (json_body.back() == '\r' || json_body.back() == '\n'))
            json_body.pop_back();

        parts.push_back({status, json_body});
        pos = next_delim;
    }
    return parts;
}

} // namespace

GmailClient::GmailClient(OAuthManager& oauth) : oauth_(oauth) {}

std::string GmailClient::get(const std::string& url) {
    std::string token = oauth_.accessToken();
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("GmailClient: curl_easy_init failed");

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error("GmailClient GET " + url + ": " + curl_easy_strerror(rc));
    if (http_code < 200 || http_code >= 300)
        throw std::runtime_error("GmailClient GET HTTP " + std::to_string(http_code) + ": " + response.substr(0, 300));
    return response;
}

std::string GmailClient::post(const std::string& url, const std::string& body,
                               const std::string& content_type) {
    std::string token = oauth_.accessToken();
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("GmailClient: curl_easy_init failed");

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token;
    std::string ct_header   = "Content-Type: " + content_type;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, ct_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error("GmailClient POST " + url + ": " + curl_easy_strerror(rc));
    if (http_code < 200 || http_code >= 300)
        throw std::runtime_error("GmailClient POST HTTP " + std::to_string(http_code) + ": " + response.substr(0, 300));
    return response;
}

std::vector<std::string> GmailClient::listMessageIds(
        const std::string& query,
        std::function<void(int)> progress_cb) {

    std::vector<std::string> ids;
    std::string page_token;
    int fetched = 0;

    do {
        std::string url = std::string(kBaseUrl) + "/messages?maxResults=500&fields=messages(id),nextPageToken,resultSizeEstimate";
        if (!query.empty())      url += "&q=" + urlEncode(query);
        if (!page_token.empty()) url += "&pageToken=" + urlEncode(page_token);

        auto resp = parseJson(get(url));

        if (resp.isMember("messages")) {
            for (const auto& m : resp["messages"])
                ids.push_back(m["id"].asString());
        }

        page_token = resp.get("nextPageToken", "").asString();
        fetched = static_cast<int>(ids.size());
        if (progress_cb) progress_cb(fetched);

    } while (!page_token.empty());

    return ids;
}

std::vector<RawMessage> GmailClient::batchGetRaw(const std::vector<std::string>& ids) {
    if (ids.empty()) return {};

    // Build multipart/mixed batch request body
    // Each sub-request: GET /gmail/v1/users/me/messages/{id}?format=raw
    static const std::string BOUNDARY = "hms_gmail_batch_boundary";
    std::string body;

    for (const auto& id : ids) {
        body += "--" + BOUNDARY + "\r\n";
        body += "Content-Type: application/http\r\n\r\n";
        body += "GET /gmail/v1/users/me/messages/" + id + "?format=raw&fields=id,threadId,labelIds,raw\r\n\r\n";
    }
    body += "--" + BOUNDARY + "--\r\n";

    std::string content_type = "multipart/mixed; boundary=" + BOUNDARY;
    std::string response = post(kBatchUrl, body, content_type);

    // Extract boundary from response Content-Type (server may use its own boundary)
    // We need to find it in the response headers — but post() only returns body.
    // The batch response uses a boundary the server chooses. We need to get the response
    // Content-Type header to find it.
    // Work-around: do a raw curl call here so we can capture headers.

    std::string token = oauth_.accessToken();
    std::string resp_body;
    std::string resp_headers_str;

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("GmailClient: curl_easy_init failed");

    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token;
    std::string ct_header   = "Content-Type: " + content_type;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, ct_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, kBatchUrl);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWriteString);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error("GmailClient batchGetRaw: " + std::string(curl_easy_strerror(rc)));
    if (http_code < 200 || http_code >= 300)
        throw std::runtime_error("GmailClient batchGetRaw HTTP " + std::to_string(http_code));

    // Extract boundary from Content-Type header
    std::string resp_boundary;
    std::regex ct_re(R"(content-type:\s*multipart/mixed;\s*boundary=([^\r\n;]+))",
                     std::regex::icase);
    std::smatch m;
    if (std::regex_search(resp_headers_str, m, ct_re)) {
        resp_boundary = m[1].str();
        // Remove surrounding quotes if present
        if (!resp_boundary.empty() && resp_boundary.front() == '"')
            resp_boundary = resp_boundary.substr(1, resp_boundary.size() - 2);
    }

    if (resp_boundary.empty())
        throw std::runtime_error("GmailClient batchGetRaw: no boundary in response headers");

    auto parts = parseBatchResponse(resp_body, resp_boundary);

    std::vector<RawMessage> results;
    results.reserve(parts.size());

    for (auto& [status, json_body] : parts) {
        if (status != 200 || json_body.empty()) continue;
        try {
            auto msg = parseJson(json_body);
            RawMessage rm;
            rm.id        = msg.get("id", "").asString();
            rm.thread_id = msg.get("threadId", "").asString();
            rm.raw_rfc822 = msg.get("raw", "").asString();
            if (msg.isMember("labelIds"))
                for (const auto& l : msg["labelIds"])
                    rm.label_ids.push_back(l.asString());
            if (!rm.id.empty() && !rm.raw_rfc822.empty())
                results.push_back(std::move(rm));
        } catch (const std::exception& e) {
            std::cerr << "[GmailClient] batchGetRaw parse error: " << e.what() << "\n";
        }
    }

    return results;
}

HistoryDelta GmailClient::listHistory(const std::string& start_history_id) {
    HistoryDelta delta;
    std::string page_token;

    do {
        std::string url = std::string(kBaseUrl) + "/history"
            "?startHistoryId=" + urlEncode(start_history_id) +
            "&historyTypes=messageAdded&historyTypes=messageDeleted"
            "&maxResults=500";
        if (!page_token.empty()) url += "&pageToken=" + urlEncode(page_token);

        auto resp = parseJson(get(url));

        delta.new_history_id = resp.get("historyId", "").asString();

        if (resp.isMember("history")) {
            for (const auto& entry : resp["history"]) {
                if (entry.isMember("messagesAdded"))
                    for (const auto& m : entry["messagesAdded"])
                        delta.added_ids.push_back(m["message"]["id"].asString());
                if (entry.isMember("messagesDeleted"))
                    for (const auto& m : entry["messagesDeleted"])
                        delta.deleted_ids.push_back(m["message"]["id"].asString());
            }
        }

        page_token = resp.get("nextPageToken", "").asString();
    } while (!page_token.empty());

    return delta;
}

void GmailClient::trashMessage(const std::string& id) {
    std::string url = std::string(kBaseUrl) + "/messages/" + id + "/trash";
    post(url, "", "application/json");
}

AttachmentData GmailClient::getAttachment(const std::string& message_id,
                                           const std::string& attachment_id) {
    std::string url = std::string(kBaseUrl) + "/messages/" + message_id +
                      "/attachments/" + attachment_id;
    auto resp = parseJson(get(url));

    AttachmentData att;
    att.attachment_id = attachment_id;
    att.data          = resp.get("data", "").asString();
    att.size_bytes    = resp.get("size", 0).asInt();
    return att;
}

std::string GmailClient::getHistoryId() {
    std::string url = std::string(kBaseUrl) + "/profile";
    auto resp = parseJson(get(url));
    return resp.get("historyId", "").asString();
}
