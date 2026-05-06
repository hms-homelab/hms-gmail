#pragma once
#include <string>
#include <pqxx/pqxx>
#include "config/AppConfig.h"

class Database {
public:
    explicit Database(const DbConfig& cfg);

    // Schema
    void ensureSchema();

    // Raw connection for services that manage their own transactions
    pqxx::connection& conn() { return *conn_; }

    std::string connString() const { return conn_str_; }

private:
    std::string conn_str_;
    std::unique_ptr<pqxx::connection> conn_;
};
