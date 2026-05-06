#include <gtest/gtest.h>
#include "config/AppConfig.h"
#include <fstream>
#include <cstdio>

static const char* TMP_YAML = "/tmp/hms_gmail_test.yaml";

TEST(AppConfig, DefaultValues) {
    AppConfig cfg;
    EXPECT_EQ(cfg.port, 8890);
    EXPECT_EQ(cfg.purge_older_than_days, 30);
    EXPECT_EQ(cfg.embedding_batch_size, 20);
    EXPECT_EQ(cfg.mqtt.port, 1883);
    EXPECT_EQ(cfg.db.name, "gmail");
}

TEST(AppConfig, DefaultDbConnString) {
    AppConfig cfg;
    auto s = cfg.db.connString();
    EXPECT_NE(s.find("host=localhost"), std::string::npos);
    EXPECT_NE(s.find("dbname=gmail"), std::string::npos);
    EXPECT_NE(s.find("port=5432"), std::string::npos);
}

TEST(AppConfig, DefaultOllamaHost) {
    AppConfig cfg;
    EXPECT_NE(cfg.ollama_host.find("11434"), std::string::npos);
}

TEST(AppConfig, DefaultMqttUser) {
    AppConfig cfg;
    EXPECT_EQ(cfg.mqtt.user, "");
}

TEST(AppConfig, LoadFromYaml) {
    std::ofstream f(TMP_YAML);
    f << "port: 9999\n"
      << "email: test@example.com\n"
      << "purge_older_than_days: 60\n"
      << "embedding_batch_size: 50\n"
      << "ollama_host: http://10.0.0.5:11434\n"
      << "mqtt:\n"
      << "  host: 10.0.0.1\n"
      << "  port: 1884\n"
      << "  user: myuser\n"
      << "  pass: mypass\n"
      << "db:\n"
      << "  host: 10.0.0.2\n"
      << "  port: 5433\n"
      << "  name: gmail_test\n"
      << "  user: dbuser\n"
      << "  pass: dbpass\n";
    f.close();

    auto cfg = AppConfig::load(TMP_YAML);
    EXPECT_EQ(cfg.port, 9999);
    EXPECT_EQ(cfg.email, "test@example.com");
    EXPECT_EQ(cfg.purge_older_than_days, 60);
    EXPECT_EQ(cfg.embedding_batch_size, 50);
    EXPECT_EQ(cfg.ollama_host, "http://10.0.0.5:11434");
    EXPECT_EQ(cfg.mqtt.host, "10.0.0.1");
    EXPECT_EQ(cfg.mqtt.port, 1884);
    EXPECT_EQ(cfg.mqtt.user, "myuser");
    EXPECT_EQ(cfg.db.host, "10.0.0.2");
    EXPECT_EQ(cfg.db.port, 5433);
    EXPECT_EQ(cfg.db.name, "gmail_test");
    EXPECT_EQ(cfg.db.user, "dbuser");

    std::remove(TMP_YAML);
}

TEST(AppConfig, LoadedConnStringReflectsYaml) {
    std::ofstream f(TMP_YAML);
    f << "db:\n  host: myhost\n  port: 5454\n  name: mydb\n  user: myuser\n  pass: mypass\n";
    f.close();

    auto cfg = AppConfig::load(TMP_YAML);
    auto s = cfg.db.connString();
    EXPECT_NE(s.find("host=myhost"), std::string::npos);
    EXPECT_NE(s.find("port=5454"), std::string::npos);
    EXPECT_NE(s.find("dbname=mydb"), std::string::npos);

    std::remove(TMP_YAML);
}

TEST(AppConfig, PartialYamlKeepsDefaults) {
    std::ofstream f(TMP_YAML);
    f << "port: 8001\n";
    f.close();

    auto cfg = AppConfig::load(TMP_YAML);
    EXPECT_EQ(cfg.port, 8001);
    // Unspecified fields keep defaults
    EXPECT_EQ(cfg.db.name, "gmail");
    EXPECT_EQ(cfg.purge_older_than_days, 30);

    std::remove(TMP_YAML);
}

TEST(AppConfig, MissingFileUsesDefaults) {
    auto cfg = AppConfig::load("/nonexistent/path.yaml");
    EXPECT_EQ(cfg.port, 8890);
    EXPECT_EQ(cfg.email, "");
}

TEST(AppConfig, EmptyYamlUsesDefaults) {
    std::ofstream f(TMP_YAML);
    f << "";
    f.close();

    auto cfg = AppConfig::load(TMP_YAML);
    EXPECT_EQ(cfg.port, 8890);
    EXPECT_EQ(cfg.db.name, "gmail");

    std::remove(TMP_YAML);
}
