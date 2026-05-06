#include "MimeParser.h"
#include <vmime/vmime.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

namespace {

std::string safeText(vmime::shared_ptr<vmime::component> val) {
    if (!val) return "";
    try {
        auto txt = vmime::dynamicCast<vmime::text>(val);
        if (txt) return txt->getConvertedText(vmime::charsets::UTF_8);
    } catch (...) {}
    try { return val->generate(); } catch (...) { return ""; }
}

std::string addrToString(vmime::shared_ptr<vmime::address> addr) {
    if (!addr) return "";
    try {
        auto mb = vmime::dynamicCast<vmime::mailbox>(addr);
        if (mb) {
            std::string name  = mb->getName().getConvertedText(vmime::charsets::UTF_8);
            std::string email = mb->getEmail().toString();
            if (name.empty()) return email;
            return name + " <" + email + ">";
        }
    } catch (...) {}
    try { return addr->generate(); } catch (...) { return ""; }
}

std::vector<std::string> addrListToVec(vmime::shared_ptr<vmime::addressList> list) {
    std::vector<std::string> out;
    if (!list) return out;
    for (size_t i = 0; i < list->getAddressCount(); ++i) {
        auto s = addrToString(list->getAddressAt(i));
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

std::string getMediaTypeString(vmime::shared_ptr<vmime::header> hdr) {
    try {
        auto f = hdr->findField(vmime::fields::CONTENT_TYPE);
        if (!f) return "text/plain";
        auto mt = vmime::dynamicCast<vmime::mediaType>(f->getValue());
        if (mt) return mt->getType() + "/" + mt->getSubType();
    } catch (...) {}
    return "text/plain";
}

std::string getCharset(vmime::shared_ptr<vmime::header> hdr) {
    try {
        auto f = hdr->findField(vmime::fields::CONTENT_TYPE);
        if (!f) return "us-ascii";
        auto ctf = vmime::dynamicCast<vmime::parameterizedHeaderField>(f);
        if (ctf) {
            auto param = ctf->getParameter("charset");
            if (param) return param->getValue().getBuffer();
        }
    } catch (...) {}
    return "us-ascii";
}

std::string getCtFilename(vmime::shared_ptr<vmime::header> hdr) {
    try {
        auto f = hdr->findField(vmime::fields::CONTENT_TYPE);
        if (!f) return "";
        auto ctf = vmime::dynamicCast<vmime::parameterizedHeaderField>(f);
        if (ctf) {
            auto param = ctf->getParameter("name");
            if (param) return param->getValue().getConvertedText(vmime::charsets::UTF_8);
        }
    } catch (...) {}
    return "";
}

void walkPart(vmime::shared_ptr<vmime::bodyPart> part,
              ParsedEmail& out, int& idx)
{
    auto hdr = part->getHeader();
    auto body = part->getBody();
    if (!body) return;

    std::string ct = getMediaTypeString(hdr);

    // Disposition
    bool is_attachment = false;
    std::string att_filename;
    try {
        auto f = hdr->findField(vmime::fields::CONTENT_DISPOSITION);
        if (f) {
            auto disp = vmime::dynamicCast<vmime::contentDisposition>(f->getValue());
            if (disp)
                is_attachment = (disp->getName() == vmime::contentDispositionTypes::ATTACHMENT);
            auto cdf = vmime::dynamicCast<vmime::parameterizedHeaderField>(f);
            if (cdf) {
                auto param = cdf->getParameter("filename");
                if (param)
                    att_filename = param->getValue().getConvertedText(vmime::charsets::UTF_8);
            }
        }
    } catch (...) {}

    if (att_filename.empty())
        att_filename = getCtFilename(hdr);

    // Recurse into multipart
    if (body->getPartCount() > 0) {
        for (size_t i = 0; i < body->getPartCount(); ++i)
            walkPart(body->getPartAt(i), out, idx);
        return;
    }

    // Attachment
    if (is_attachment || (!att_filename.empty() && ct != "text/plain" && ct != "text/html")) {
        ParsedAttachment att;
        att.filename   = att_filename;
        att.mime_type  = ct;
        att.part_index = idx++;
        try {
            auto contents = body->getContents();
            if (contents) {
                vmime::string buf;
                vmime::utility::outputStreamStringAdapter os(buf);
                contents->extract(os);
                att.size_bytes = static_cast<int>(buf.size());
            }
        } catch (...) {}
        out.attachments.push_back(att);
        out.has_attachment = true;
        return;
    }

    // Text body
    try {
        auto contents = body->getContents();
        if (!contents) return;

        vmime::string raw;
        vmime::utility::outputStreamStringAdapter os(raw);
        contents->extract(os);

        std::string charset = getCharset(hdr);
        if (charset.empty()) charset = "us-ascii";

        vmime::string utf8;
        try {
            vmime::charsetConverter::create(
                vmime::charset(charset), vmime::charsets::UTF_8)
                ->convert(raw, utf8);
        } catch (...) {
            utf8 = raw;
        }

        if (ct == "text/html") {
            if (out.body_html.empty()) out.body_html = std::string(utf8.begin(), utf8.end());
        } else {
            if (out.body_text.empty()) out.body_text = std::string(utf8.begin(), utf8.end());
        }
    } catch (...) {}
}

} // namespace

ParsedEmail MimeParser::parse(const std::string& raw_eml) {
    ParsedEmail out;
    if (raw_eml.empty()) return out;
    try {
        vmime::string vmraw(raw_eml.begin(), raw_eml.end());
        auto msg = vmime::make_shared<vmime::message>();
        msg->parse(vmraw);

        auto hdr = msg->getHeader();

        // Message-ID
        try {
            auto f = hdr->findField(vmime::fields::MESSAGE_ID);
            if (f) {
                auto mid = vmime::dynamicCast<vmime::messageId>(f->getValue());
                if (mid) out.message_id = mid->getId();
                else     out.message_id = safeText(f->getValue());
            }
        } catch (...) {}

        // Subject
        try {
            auto f = hdr->findField(vmime::fields::SUBJECT);
            if (f) out.subject = safeText(f->getValue());
        } catch (...) {}

        // Date (raw string for later ISO conversion)
        try {
            auto f = hdr->findField(vmime::fields::DATE);
            if (f) out.date_str = f->generate();
        } catch (...) {}

        // From
        try {
            auto f = hdr->findField(vmime::fields::FROM);
            if (f) {
                auto mbl = vmime::dynamicCast<vmime::mailboxList>(f->getValue());
                if (mbl && mbl->getMailboxCount() > 0)
                    out.from_addr = addrToString(mbl->getMailboxAt(0));
                else {
                    auto mb = vmime::dynamicCast<vmime::mailbox>(f->getValue());
                    if (mb) out.from_addr = addrToString(mb);
                }
            }
        } catch (...) {}

        // To
        try {
            auto f = hdr->findField(vmime::fields::TO);
            if (f) {
                auto al = vmime::dynamicCast<vmime::addressList>(f->getValue());
                if (al) out.to_addrs = addrListToVec(al);
            }
        } catch (...) {}

        // CC
        try {
            auto f = hdr->findField(vmime::fields::CC);
            if (f) {
                auto al = vmime::dynamicCast<vmime::addressList>(f->getValue());
                if (al) out.cc = addrListToVec(al);
            }
        } catch (...) {}

        // Body
        int idx = 0;
        walkPart(vmime::dynamicCast<vmime::bodyPart>(msg), out, idx);

    } catch (const std::exception& e) {
        std::cerr << "[MimeParser] " << e.what() << "\n";
    }
    return out;
}

ParsedEmail MimeParser::parseBase64Url(const std::string& b64url) {
    // Gmail API returns RFC822 base64url-encoded (RFC 4648 §5: + → -, / → _, no padding)
    // Convert to standard base64 and decode via vmime utility
    std::string b64 = b64url;
    for (char& c : b64) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
    }
    // Add padding
    while (b64.size() % 4 != 0) b64 += '=';

    vmime::string decoded;
    vmime::utility::outputStreamStringAdapter os(decoded);
    vmime::utility::inputStreamStringAdapter is(b64);
    vmime::utility::encoder::encoderFactory::getInstance()
        ->create("base64")->decode(is, os);

    return parse(std::string(decoded.begin(), decoded.end()));
}

ParsedEmail MimeParser::parseFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}
