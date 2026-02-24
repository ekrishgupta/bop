#pragma once

#include "core.hpp"
#include "logic.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <initializer_list>
#include <type_traits>

namespace bop {

struct ExecutionEngine {
  std::atomic<bool> is_running{false};
  mutable std::vector<const MarketBackend *> backends_;

  virtual ~ExecutionEngine() = default;

  void register_backend(const MarketBackend *backend) {
    backends_.push_back(backend);
  }

  void sync_all_markets() {
    for (auto b : backends_) {
      std::cout << "[ENGINE] Syncing markets for " << b->name() << "..." << std::endl;
      const_cast<MarketBackend *>(b)->sync_markets();
    }
  }

  virtual int64_t get_position(MarketId market) const {
    return 0;
  }

  virtual Price get_balance() const {
    return Price(0);
  }

  virtual Price get_exposure() const { return Price(0); }

  virtual Price get_pnl() const {
    return Price(0);
  }

  virtual Price get_depth(MarketId market, bool is_bid) const {
    for (auto b : backends_) {
        Price p = b->get_depth(market, is_bid);
        if (p.raw > 0) return p;
    }
    return Price(0);
  }

  virtual Price get_price(MarketId market, bool outcome_yes) const {
    for (auto b : backends_) {
        Price p = b->get_price(market, outcome_yes);
        if (p.raw > 0) return p;
    }
    return Price(0);
  }

  virtual int64_t get_volume(MarketId market) const { 
    for (auto b : backends_) {
        int64_t v = b->clob_get_last_trade_price(market).raw; // Fallback or specific volume call
        // Many backends don't have a direct get_volume in the base class yet
    }
    return 0; 
  }

  virtual void stop() { is_running = false; }
  virtual void run();
};

class LiveExecutionEngine : public ExecutionEngine {
  mutable std::mutex mtx;
  Price cached_balance{0};
  std::unordered_map<uint32_t, int64_t> cached_positions;
  std::thread sync_thread;

public:
  LiveExecutionEngine() = default;
  ~LiveExecutionEngine() {
    stop();
    if (sync_thread.joinable()) sync_thread.join();
  }

  int64_t get_position(MarketId market) const override {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cached_positions.find(market.hash);
    return (it != cached_positions.end()) ? it->second : 0;
  }

  Price get_balance() const override {
    std::lock_guard<std::mutex> lock(mtx);
    return cached_balance;
  }

  void run() override {
    is_running = true;
    sync_thread = std::thread([this]() {
      while (is_running) {
        sync_state();
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });

    std::cout << "[LIVE ENGINE] Starting event loop..." << std::endl;
    while (is_running) {
      GlobalAlgoManager.tick(*this);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

private:
  void sync_state() {
    Price total_balance(0);
    std::unordered_map<uint32_t, int64_t> new_positions;

    for (auto b : backends_) {
      total_balance = total_balance + b->get_balance();
      
      std::string pos_json = b->get_positions();
      try {
        auto j = nlohmann::json::parse(pos_json);
        
        // Handle array directly (Polymarket CLOB style)
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
        } 
        // Handle Kalshi v2 style
        else if (j.contains("market_positions")) {
            for (const auto &p : j["market_positions"]) {
                std::string ticker = p["ticker"];
                int64_t qty = p["position"].get<int64_t>();
                new_positions[fnv1a(ticker.c_str())] += qty;
            }
        }
        // Handle generic 'positions' object
        else if (j.contains("positions")) {
          for (const auto &p : j["positions"]) {
            std::string ticker;
            if (p.contains("market_ticker")) ticker = p["market_ticker"];
            else if (p.contains("token_id")) ticker = p["token_id"];
            else if (p.contains("ticker")) ticker = p["ticker"];

            if (!ticker.empty()) {
                int64_t qty = 0;
                if (p.contains("quantity")) qty = p["quantity"].get<int64_t>();
                else if (p.contains("position")) qty = p["position"].get<int64_t>();
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
    }
    std::cout << "[LIVE ENGINE] Synced state: Balance=" << cached_balance << " Markets=" << cached_positions.size() << std::endl;
  }
};

} // namespace bop

extern bop::ExecutionEngine &LiveExchange;

#include "algo_manager.hpp"

namespace bop {
inline void ExecutionEngine::run() {
  is_running = true;
  std::cout << "[ENGINE] Starting event loop..." << std::endl;
  while (is_running) {
    GlobalAlgoManager.tick(*this);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "[ENGINE] Event loop stopped." << std::endl;
}
} // namespace bop

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &engine) {
  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t latency = now - o.creation_timestamp_ns;

  if (o.algo_type != bop::AlgoType::None) {
    std::cout << "[ALGO] Registering " << (int)o.algo_type << " for "
              << o.market.ticker << std::endl;
    bop::GlobalAlgoManager.submit(o);
    return;
  }

  if (o.backend) {
    if (o.is_spread) {
      std::cout << "[BACKEND] Dispatching spread order (" << o.market.hash
                << " - " << o.market2.hash << ") to " << o.backend->name()
                << " (" << latency << " ns latency)" << std::endl;
    } else {
      std::cout << "[BACKEND] Dispatching to " << o.backend->name() << " ("
                << latency << " ns latency)" << std::endl;
    }
    o.backend->create_order(o);
  } else {
    if (o.is_spread) {
      std::cout << "[ENGINE] No backend bound for spread order ("
                << o.market.hash << " - " << o.market2.hash
                << "). Simulated latency: " << latency << " ns." << std::endl;
    } else {
      std::cout << "[ENGINE] No backend bound. Simulated latency: " << latency
                << " ns." << std::endl;
    }
  }
}

inline void operator>>(std::initializer_list<bop::Order> batch,
                       bop::ExecutionEngine &engine) {
  if (batch.size() == 0)
    return;

  const bop::MarketBackend *common_backend = batch.begin()->backend;
  bool all_same = true;
  for (const auto &o : batch) {
    if (o.backend != common_backend) {
      all_same = false;
      break;
    }
  }

  if (all_same && common_backend) {
    std::cout << "[BATCH] Dispatching " << batch.size() << " orders to "
              << common_backend->name() << std::endl;
    std::vector<bop::Order> orders(batch);
    common_backend->create_batch_orders(orders);
  } else {
    std::cout << "[BATCH] Heterogeneous batch. Dispatching individually..."
              << std::endl;
    for (const auto &o : batch) {
      o >> engine;
    }
  }
}

namespace bop {

template <typename Tag> inline bool RelativeCondition<Tag>::eval() const {
  if constexpr (std::is_same_v<Tag, PriceTag>) {
    Price l_val = left.backend
                      ? left.backend->get_price(left.market, left.outcome_yes)
                      : LiveExchange.get_price(left.market, left.outcome_yes);
    Price r_val =
        right.backend
            ? right.backend->get_price(right.market, right.outcome_yes)
            : LiveExchange.get_price(right.market, right.outcome_yes);
    return is_greater ? l_val > r_val : l_val < r_val;
  } else {
    int64_t l_val = 0;
    int64_t r_val = 0;
    return is_greater ? l_val > r_val : l_val < r_val;
  }
}

template <typename Tag, typename Q>
inline bool Condition<Tag, Q>::eval() const {
  if constexpr (std::is_same_v<Tag, PositionTag>) {
    int64_t val = LiveExchange.get_position(query.market);
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, BalanceTag>) {
    Price val = LiveExchange.get_balance();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, ExposureTag>) {
    Price val = LiveExchange.get_exposure();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, PnLTag>) {
    Price val = LiveExchange.get_pnl();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, PriceTag>) {
    Price val = query.backend
                    ? query.backend->get_price(query.market, query.outcome_yes)
                    : LiveExchange.get_price(query.market, query.outcome_yes);
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, DepthTag>) {
    Price val = query.backend
                    ? query.backend->get_depth(query.market, query.outcome_yes)
                    : LiveExchange.get_depth(query.market, query.outcome_yes);
    return is_greater ? val.raw > threshold : val.raw < threshold;
  }
  return false;
}

} // namespace bop

template <typename T>
inline void operator>>(const bop::ConditionalOrder<T> &co,
                       bop::ExecutionEngine &engine) {
  if (co.condition.eval()) {
    std::cout << "[CONDITION] Passed conditional evaluation. Dispatching..."
              << std::endl;
    co.order >> engine;
  } else {
    std::cout << "[CONDITION] Failed conditional evaluation. Suppressing order."
              << std::endl;
  }
}

inline void operator>>(const bop::OCOOrder &oco, bop::ExecutionEngine &engine) {
  std::cout << "[OCO] Dispatching OCO pair..." << std::endl;
  oco.order1 >> engine;
  oco.order2 >> engine;
}
