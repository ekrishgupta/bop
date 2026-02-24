#pragma once

#include "core.hpp"
#include <sqlite3.h>
#include <string>
#include <mutex>
#include <vector>
#include <iostream>

namespace bop {

class Database {
public:
    explicit Database(const std::string& db_path = "bop_trading.db");
    ~Database();

    // Prevent copying
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void log_order(const std::string& id, const Order& o);
    void log_fill(const std::string& id, int qty, Price price);
    void log_status(const std::string& id, OrderStatus status);
    void log_pnl_snapshot(Price balance, Price pnl, int64_t daily_pnl_raw);

private:
    sqlite3* db_ = nullptr;
    std::mutex mtx_;

    void init_schema();
    void execute(const std::string& sql);
};

} // namespace bop
