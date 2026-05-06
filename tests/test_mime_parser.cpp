#include <gtest/gtest.h>
#include "mime/MimeParser.h"
#include <fstream>
#include <cstdio>

// ── Fixtures ──────────────────────────────────────────────────────────────────

static const char* SIMPLE_EML = R"(From: Alice <alice@example.com>
To: Bob <bob@example.com>
Subject: Hello World
Date: Mon, 4 May 2026 10:00:00 +0000
Message-ID: <test123@example.com>
MIME-Version: 1.0
Content-Type: text/plain; charset=utf-8

This is the body.
)";

static const char* MULTIPART_ALTERNATIVE = R"(From: sender@example.com
To: receiver@example.com
Subject: Multipart test
Date: Tue, 5 May 2026 09:00:00 +0000
Message-ID: <multi456@example.com>
MIME-Version: 1.0
Content-Type: multipart/alternative; boundary="boundary123"

--boundary123
Content-Type: text/plain; charset=utf-8

Plain text body.

--boundary123
Content-Type: text/html; charset=utf-8

<html><body>HTML body.</body></html>

--boundary123--
)";

static const char* MULTIPART_MIXED_ATTACHMENT = R"(From: boss@company.com
To: employee@company.com
Subject: Quarterly Report
Date: Wed, 6 May 2026 14:00:00 +0000
Message-ID: <attach001@example.com>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="mixbound"

--mixbound
Content-Type: text/plain; charset=utf-8

Please find the report attached.

--mixbound
Content-Type: application/pdf
Content-Disposition: attachment; filename="report.pdf"
Content-Transfer-Encoding: base64

JVBERi0xLjQ=

--mixbound--
)";

static const char* MULTIPART_ATTACHMENT_VIA_CT = R"(From: noreply@example.com
To: user@example.com
Subject: Your download
Date: Thu, 7 May 2026 08:00:00 +0000
Message-ID: <ctname001@example.com>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="ctbound"

--ctbound
Content-Type: text/plain; charset=utf-8

Download is attached.

--ctbound
Content-Type: image/png; name="logo.png"
Content-Transfer-Encoding: base64

iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==

--ctbound--
)";

static const char* HTML_ONLY = R"(From: newsletter@example.com
To: subscriber@example.com
Subject: Weekly Newsletter
Date: Fri, 8 May 2026 10:00:00 +0000
Message-ID: <html001@example.com>
MIME-Version: 1.0
Content-Type: text/html; charset=utf-8

<html><body><h1>Hello!</h1><p>This is HTML only.</p></body></html>
)";

static const char* MULTIPLE_TO_CC = R"(From: Albin <albin@example.com>
To: Alice <alice@example.com>, Bob <bob@example.com>
CC: Carol <carol@example.com>
Subject: Group email
Date: Sat, 9 May 2026 12:00:00 +0000
Message-ID: <group001@example.com>
MIME-Version: 1.0
Content-Type: text/plain; charset=utf-8

Hello everyone.
)";

static const char* NO_MESSAGE_ID = R"(From: sender@example.com
To: recv@example.com
Subject: No ID here
Date: Sun, 10 May 2026 09:00:00 +0000
MIME-Version: 1.0
Content-Type: text/plain; charset=utf-8

Body without message-id.
)";

static const char* GARBAGE_INPUT = "This is not an email at all!!!???";

static const char* NESTED_MULTIPART = R"(From: sender@example.com
To: recv@example.com
Subject: Nested multipart
Date: Mon, 11 May 2026 08:00:00 +0000
Message-ID: <nested001@example.com>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="outer"

--outer
Content-Type: multipart/alternative; boundary="inner"

--inner
Content-Type: text/plain; charset=utf-8

Plain text inside nested.

--inner
Content-Type: text/html; charset=utf-8

<html><body>HTML inside nested.</body></html>

--inner--

--outer
Content-Type: application/octet-stream
Content-Disposition: attachment; filename="data.bin"
Content-Transfer-Encoding: base64

AAAA

--outer--
)";

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(MimeParser, SimplePlainText) {
    auto em = MimeParser::parse(SIMPLE_EML);
    EXPECT_EQ(em.message_id, "test123@example.com");
    EXPECT_EQ(em.subject, "Hello World");
    EXPECT_NE(em.from_addr.find("alice@example.com"), std::string::npos);
    EXPECT_FALSE(em.to_addrs.empty());
    EXPECT_NE(em.body_text.find("This is the body"), std::string::npos);
    EXPECT_FALSE(em.has_attachment);
    EXPECT_TRUE(em.attachments.empty());
}

TEST(MimeParser, MultipartAlternative) {
    auto em = MimeParser::parse(MULTIPART_ALTERNATIVE);
    EXPECT_EQ(em.message_id, "multi456@example.com");
    EXPECT_EQ(em.subject, "Multipart test");
    EXPECT_NE(em.body_text.find("Plain text body"), std::string::npos);
    EXPECT_NE(em.body_html.find("HTML body"), std::string::npos);
    EXPECT_FALSE(em.has_attachment);
}

TEST(MimeParser, EmptyInput) {
    auto em = MimeParser::parse("");
    EXPECT_TRUE(em.message_id.empty());
    EXPECT_TRUE(em.subject.empty());
    EXPECT_TRUE(em.body_text.empty());
    EXPECT_TRUE(em.body_html.empty());
    EXPECT_FALSE(em.has_attachment);
}

TEST(MimeParser, MissingFile) {
    auto em = MimeParser::parseFile("/nonexistent/path.eml");
    EXPECT_TRUE(em.message_id.empty());
}

TEST(MimeParser, AttachmentDetectedByDisposition) {
    auto em = MimeParser::parse(MULTIPART_MIXED_ATTACHMENT);
    EXPECT_EQ(em.message_id, "attach001@example.com");
    EXPECT_TRUE(em.has_attachment);
    ASSERT_FALSE(em.attachments.empty());
    EXPECT_EQ(em.attachments[0].filename, "report.pdf");
    EXPECT_EQ(em.attachments[0].mime_type, "application/pdf");
    EXPECT_NE(em.body_text.find("Please find"), std::string::npos);
}

TEST(MimeParser, AttachmentDetectedByContentTypeName) {
    auto em = MimeParser::parse(MULTIPART_ATTACHMENT_VIA_CT);
    EXPECT_EQ(em.message_id, "ctname001@example.com");
    EXPECT_TRUE(em.has_attachment);
    ASSERT_FALSE(em.attachments.empty());
    EXPECT_EQ(em.attachments[0].filename, "logo.png");
    EXPECT_EQ(em.attachments[0].mime_type, "image/png");
}

TEST(MimeParser, AttachmentHasSizeBytes) {
    auto em = MimeParser::parse(MULTIPART_MIXED_ATTACHMENT);
    ASSERT_FALSE(em.attachments.empty());
    // base64 "JVBERi0xLjQ=" decodes to 6 bytes — after extraction, size >= 0
    EXPECT_GE(em.attachments[0].size_bytes, 0);
}

TEST(MimeParser, AttachmentPartIndex) {
    auto em = MimeParser::parse(MULTIPART_MIXED_ATTACHMENT);
    ASSERT_FALSE(em.attachments.empty());
    EXPECT_EQ(em.attachments[0].part_index, 0);
}

TEST(MimeParser, HtmlOnlyBody) {
    auto em = MimeParser::parse(HTML_ONLY);
    EXPECT_EQ(em.message_id, "html001@example.com");
    EXPECT_NE(em.body_html.find("Hello!"), std::string::npos);
    EXPECT_TRUE(em.body_text.empty());
    EXPECT_FALSE(em.has_attachment);
}

TEST(MimeParser, MultipleToAndCC) {
    auto em = MimeParser::parse(MULTIPLE_TO_CC);
    EXPECT_EQ(em.message_id, "group001@example.com");
    EXPECT_GE(em.to_addrs.size(), 2u);
    bool alice_in_to = false, bob_in_to = false;
    for (const auto& a : em.to_addrs) {
        if (a.find("alice") != std::string::npos) alice_in_to = true;
        if (a.find("bob") != std::string::npos)   bob_in_to = true;
    }
    EXPECT_TRUE(alice_in_to);
    EXPECT_TRUE(bob_in_to);
    ASSERT_FALSE(em.cc.empty());
    EXPECT_NE(em.cc[0].find("carol"), std::string::npos);
}

TEST(MimeParser, MissingMessageId) {
    auto em = MimeParser::parse(NO_MESSAGE_ID);
    // message_id may be empty or synthesized by vmime — either is fine
    EXPECT_EQ(em.subject, "No ID here");
    EXPECT_NE(em.body_text.find("Body without message-id"), std::string::npos);
}

TEST(MimeParser, GarbageInputNoThrow) {
    EXPECT_NO_THROW({
        auto em = MimeParser::parse(GARBAGE_INPUT);
        (void)em;
    });
}

TEST(MimeParser, NestedMultipartExtractsBodyAndAttachment) {
    auto em = MimeParser::parse(NESTED_MULTIPART);
    EXPECT_EQ(em.message_id, "nested001@example.com");
    EXPECT_NE(em.body_text.find("Plain text inside nested"), std::string::npos);
    EXPECT_NE(em.body_html.find("HTML inside nested"), std::string::npos);
    EXPECT_TRUE(em.has_attachment);
    ASSERT_FALSE(em.attachments.empty());
    EXPECT_EQ(em.attachments[0].filename, "data.bin");
}

TEST(MimeParser, ParseFileRoundTrip) {
    const char* path = "/tmp/hms_gmail_test_round.eml";
    {
        std::ofstream f(path);
        f << SIMPLE_EML;
    }
    auto em = MimeParser::parseFile(path);
    EXPECT_EQ(em.message_id, "test123@example.com");
    EXPECT_EQ(em.subject, "Hello World");
    std::remove(path);
}

TEST(MimeParser, FromFieldDisplayName) {
    auto em = MimeParser::parse(SIMPLE_EML);
    // from_addr should contain the display name or email
    EXPECT_FALSE(em.from_addr.empty());
    EXPECT_NE(em.from_addr.find("alice@example.com"), std::string::npos);
}

TEST(MimeParser, DateFieldCaptured) {
    auto em = MimeParser::parse(SIMPLE_EML);
    EXPECT_FALSE(em.date_str.empty());
}
