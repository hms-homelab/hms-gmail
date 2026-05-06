#pragma once
#include <string>
#include <vector>
#include <vmime/vmime.hpp>

struct ParsedAttachment {
    std::string filename;
    std::string mime_type;
    int         size_bytes = 0;
    int         part_index = 0;
};

struct ParsedEmail {
    std::string message_id;
    std::string thread_id;   // populated by Indexer from gyb sqlite
    std::string from_addr;
    std::vector<std::string> to_addrs;
    std::vector<std::string> cc;
    std::string subject;
    std::string date_str;    // RFC2822 raw
    std::string body_text;
    std::string body_html;
    std::vector<ParsedAttachment> attachments;
    bool has_attachment = false;
};

class MimeParser {
public:
    // Parse raw RFC822 content from memory
    static ParsedEmail parse(const std::string& raw_eml);

    // Decode base64url (Gmail API format=raw) then parse
    static ParsedEmail parseBase64Url(const std::string& b64url);

    // Convenience: read file then parse
    static ParsedEmail parseFile(const std::string& path);
};
