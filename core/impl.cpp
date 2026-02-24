#include "engine.hpp"
#include "streaming_backend.hpp"
#include "algo_manager.hpp"
#include "algo.hpp"
#include <iostream>

namespace bop {

AlgoManager GlobalAlgoManager;
OrderTracker GlobalOrderTracker;

// --- ExecutionEngine ---

void ExecutionEngine::run() {
    is_running = true;
    std::cout << "[ENGINE] Starting default responsive event loop..." << std::endl;
    while (is_running) {
      GlobalAlgoManager.tick(*this);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// --- LiveExecutionEngine ---

LiveExecutionEngine::~LiveExecutionEngine() {
    stop();
    if (sync_thread.joinable()) sync_thread.join();
    tick_cv.notify_all();
}

int64_t LiveExecutionEngine::get_position(MarketId market) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cached_positions.find(market.hash);
    return (it != cached_positions.end()) ? it->second : 0;
}

Price LiveExecutionEngine::get_balance() const {
    std::lock_guard<std::mutex> lock(mtx);
    return cached_balance;
}

Price LiveExecutionEngine::get_exposure() const {
    std::lock_guard<std::mutex> lock(mtx);
    Price total_exposure(0);
    return total_exposure;
}

Price LiveExecutionEngine::get_pnl() const {
    return Price(0);
}

void LiveExecutionEngine::run() {
    is_running = true;

    std::cout << "[LIVE ENGINE] Performing initial state sync..." << std::endl;
    sync_state();

    sync_thread = std::thread([this]() {
      while (is_running) {
        sync_state();
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });

    std::cout << "[LIVE ENGINE] Starting responsive event loop (WebSocket driven)..."
              << std::endl;
    while (is_running) {
      {
        std::unique_lock<std::mutex> lock(tick_mtx);
        tick_cv.wait_for(lock, std::chrono::milliseconds(100));
      }

      if (!is_running) break;

      GlobalAlgoManager.tick(*this);
      check_kill_switch();
    }
}

void LiveExecutionEngine::sync_state() {
    Price total_balance(0);
    std::unordered_map<uint32_t, int64_t> new_positions;

    for (auto b : backends_) {
      total_balance = total_balance + b->get_balance();
      
      std::string pos_json = b->get_positions();
      try {
        auto j = nlohmann::json::parse(pos_json);
        if (j.is_array()) {
            for (const auto &p : j) {
                std::string ticker;
                if (p.contains("asset_id")) ticker = p["asset_id"];
                else if (p.contains("token_id")) ticker = p["token_id"];

                if (!ticker.empty() && p.contains("size")) {
                    int64_t qty = std::stoll(p["size"].get<std::string>());
                    new_positions[fnv1a(ticker.c_str())] += qty;
                }
            }
        } else if (j.contains("positions")) {
          for (const auto &p : j["positions"]) {
            std::string ticker;
            if (p.contains("market_ticker")) ticker = p["market_ticker"];
            else if (p.contains("ticker")) ticker = p["ticker"];

            if (!ticker.empty()) {
                int64_t qty = 0;
                if (p.contains("quantity")) qty = p["quantity"].get<int64_t>();
                else if (p.contains("size")) {
                    if (p["size"].is_string()) qty = std::stoll(p["size"].get<std::string>());
                    else qty = p["size"].get<int64_t>();
                }
                new_positions[fnv1a(ticker.c_str())] += qty;
            }
          }
        }
      } catch (...) {}
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      cached_balance = total_balance;
      cached_positions = std::move(new_positions);
      db.log_pnl_snapshot(cached_balance, get_pnl(), current_daily_pnl_raw.load());
    }
    std::cout << "[LIVE ENGINE] Synced state: Balance=" << cached_balance << " Markets=" << cached_positions.size() << std::endl;
}

// --- StreamingMarketBackend ---

void StreamingMarketBackend::update_price(MarketId market, Price yes, Price no) {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      price_cache_[market.hash] = {yes, no};
    }
    if (engine_)
      engine_->trigger_tick();
}

void StreamingMarketBackend::update_orderbook(MarketId market, const OrderBook &ob) {
    std::function<void(const OrderBook &)> cb;
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      orderbook_cache_[market.hash] = ob;
      auto it = callbacks_.find(market.hash);
      if (it != callbacks_.end()) {
        cb = it->second;
      }
    }
    if (cb)
      cb(ob);
    if (engine_)
      engine_->trigger_tick();
}

void StreamingMarketBackend::notify_fill(const std::string &id, int qty, Price price) {
    if (engine_) {
        engine_->add_order_fill(id, qty, price);
    }
}

void StreamingMarketBackend::notify_status(const std::string &id, OrderStatus status) {
    if (engine_) {
        engine_->update_order_status(id, status);
    }
}

} // namespace bop
