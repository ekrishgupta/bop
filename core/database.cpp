#include "database.hpp"
#include <chrono>
#include <sstream>

namespace bop {

Database::Database(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[DB] Failed to open database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
    } else {
        init_schema();
    }
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::init_schema() {
    execute("CREATE TABLE IF NOT EXISTS orders ("
            "id TEXT PRIMARY KEY, "
            "ticker TEXT, "
            "is_buy INTEGER, "
            "quantity INTEGER, "
            "price INTEGER, "
            "outcome_yes INTEGER, "
            "timestamp_ns INTEGER"
            ");");

    execute("CREATE TABLE IF NOT EXISTS fills ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "order_id TEXT, "
            "qty INTEGER, "
            "price INTEGER, "
            "timestamp_ms INTEGER"
            ");");

    execute("CREATE TABLE IF NOT EXISTS status_updates ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "order_id TEXT, "
            "status INTEGER, "
            "timestamp_ms INTEGER"
            ");");

    execute("CREATE TABLE IF NOT EXISTS pnl_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "balance INTEGER, "
            "pnl INTEGER, "
            "daily_pnl_raw INTEGER, "
            "timestamp_ms INTEGER"
            ");");
}

void Database::execute(const std::string& sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[DB] SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

void Database::log_order(const std::string& id, const Order& o) {
    if (!db_) return;
    std::lock_guard<std::mutex> lock(mtx_);

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO orders (id, ticker, is_buy, quantity, price, outcome_yes, timestamp_ns) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, o.market.ticker.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, o.is_buy ? 1 : 0);
        sqlite3_bind_int64(stmt, 4, o.quantity);
        sqlite3_bind_int64(stmt, 5, o.price.raw);
        sqlite3_bind_int(stmt, 6, o.outcome_yes ? 1 : 0);
        sqlite3_bind_int64(stmt, 7, o.creation_timestamp_ns);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[DB] Failed to log order: " << sqlite3_errmsg(db_) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

void Database::log_fill(const std::string& id, int qty, Price price) {
    if (!db_) return;
    std::lock_guard<std::mutex> lock(mtx_);

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO fills (order_id, qty, price, timestamp_ms) VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, qty);
        sqlite3_bind_int64(stmt, 3, price.raw);
        sqlite3_bind_int64(stmt, 4, now);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[DB] Failed to log fill: " << sqlite3_errmsg(db_) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

void Database::log_status(const std::string& id, OrderStatus status) {
    if (!db_) return;
    std::lock_guard<std::mutex> lock(mtx_);

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO status_updates (order_id, status, timestamp_ms) VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, (int)status);
        sqlite3_bind_int64(stmt, 3, now);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[DB] Failed to log status: " << sqlite3_errmsg(db_) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

void Database::log_pnl_snapshot(Price balance, Price pnl, int64_t daily_pnl_raw) {
    if (!db_) return;
    std::lock_guard<std::mutex> lock(mtx_);

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO pnl_history (balance, pnl, daily_pnl_raw, timestamp_ms) VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, balance.raw);
        sqlite3_bind_int64(stmt, 2, pnl.raw);
        sqlite3_bind_int64(stmt, 3, daily_pnl_raw);
        sqlite3_bind_int64(stmt, 4, now);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[DB] Failed to log PnL: " << sqlite3_errmsg(db_) << std::endl;
        }
        sqlite3_finalize(stmt);
    }
}

} // namespace bop
