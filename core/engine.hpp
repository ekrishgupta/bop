#pragma once

#include "core.hpp"
#include "logic.hpp"
#include "order_tracker.hpp"
#include "streaming_backend.hpp"
#include "database.hpp"
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

struct RiskLimits {
  int64_t max_position_size = 10000;
  Price max_market_exposure = Price::from_usd(5000);
  Price max_sector_exposure = Price::from_usd(15000);
  double fat_finger_threshold = 0.10; // 10% deviation from BBO
  Price daily_loss_limit = Price::from_usd(1000);
};

struct ExecutionEngine {
  std::atomic<bool> is_running{false};
  mutable std::vector<const MarketBackend *> backends_;
  OrderTracker order_store;
  RiskLimits limits;
  std::atomic<int64_t> current_daily_pnl_raw{0};
  std::unordered_map<std::string, std::string> market_to_sector;
  mutable std::mutex risk_mtx;
  Database db;

  virtual ~ExecutionEngine() = default;

  void set_sector(const std::string &ticker, const std::string &sector) {
    market_to_sector[ticker] = sector;
  }

  std::string get_sector(const std::string &ticker) const {
    auto it = market_to_sector.find(ticker);
    return (it != market_to_sector.end()) ? it->second : "Default";
  }

  void register_backend(const MarketBackend *backend) {
    backends_.push_back(backend);
    auto streaming = dynamic_cast<const StreamingMarketBackend *>(backend);
    if (streaming) {
      const_cast<StreamingMarketBackend *>(streaming)->set_engine(this);
    }
  }

  void sync_all_markets() {
    for (auto b : backends_) {
      std::cout << "[ENGINE] Syncing markets for " << b->name() << "..."
                << std::endl;
      const_cast<MarketBackend *>(b)->sync_markets();
    }
  }

  // Order Tracking
  void track_order(const std::string &id, const Order &o) {
    db.log_order(id, o);
    order_store.track(id, o);
  }

  void update_order_status(const std::string &id, OrderStatus status) {
    db.log_status(id, status);
    order_store.update_status(id, status);
  }

  void add_order_fill(const std::string &id, int qty, Price price) {
    // 1. Update persistent store
    db.log_fill(id, qty, price);

    // 2. Update order store
    order_store.add_fill(id, qty, price);

    // 3. Real-time PnL tracking (simplified realized PnL delta)
    // In a real system, we'd compare price to cost basis.
    // For demonstration of kill-switch teeth, we'll assume a 1% slippage loss
    // on every fill to simulate a "bad day" if many orders fill.
    int64_t simulated_loss = (price.raw * qty) / 100;
    current_daily_pnl_raw -= simulated_loss;

    std::cout << "[ENGINE] Fill recorded for " << id << ": " << qty << " @ "
              << price << " (Daily PnL: " << Price(current_daily_pnl_raw.load())
              << ")" << std::endl;

    check_kill_switch();
  }

  // Risk Management
  bool check_risk(const Order &o) const {
    std::lock_guard<std::mutex> lock(risk_mtx);

    // 0. Kill-Switch Check
    if (current_daily_pnl_raw.load() <= -limits.daily_loss_limit.raw) {
      std::cerr << "[RISK] REJECT: Kill-switch is ACTIVE. Daily loss: "
                << Price(current_daily_pnl_raw.load()) << std::endl;
      return false;
    }

    // 1. Max Position Size
    int64_t current_pos = get_position(o.market);
    int64_t new_pos = current_pos + (o.is_buy ? o.quantity : -o.quantity);
    if (std::abs(new_pos) > limits.max_position_size) {
      std::cerr << "[RISK] REJECT: Max position size exceeded for "
                << o.market.ticker << " (Current: " << current_pos
                << ", Requested: " << o.quantity << ")" << std::endl;
      return false;
    }

    // 2. Sector Exposure
    std::string sector = get_sector(o.market.ticker);
    Price sector_exposure(0);
    // (In a real implementation, we'd iterate cached_positions and sum by
    // sector)
    // For now, let's just log that we are checking the sector.
    // std::cout << "[RISK] Checking sector: " << sector << std::endl;

    // 3. Fat-Finger Price Protection
    if (o.price.raw > 0) {
      Price bbo = get_price(o.market, o.outcome_yes);
      if (bbo.raw > 0) {
        double deviation = std::abs((double)o.price.raw - bbo.raw) / bbo.raw;
        if (deviation > limits.fat_finger_threshold) {
          std::cerr << "[RISK] REJECT: Fat-finger protection! Price " << o.price
                    << " deviates " << (deviation * 100) << "% from market "
                    << bbo << std::endl;
          return false;
        }
      }
    }

    return true;
  }

  void check_kill_switch() {
    if (current_daily_pnl_raw.load() <= -limits.daily_loss_limit.raw) {
      std::cerr << "[RISK] CRITICAL: Daily loss limit hit ("
                << Price(current_daily_pnl_raw.load())
                << "). Activating kill-switch." << std::endl;
      stop();
    }
  }

  std::vector<OrderRecord> get_orders() {
    return order_store.get_all();
  }

  virtual void stop() {
    is_running = false;
  }

  virtual size_t get_open_order_count(MarketId market) const {
    return const_cast<ExecutionEngine *>(this)->order_store.count_open(market);
  }

  virtual int64_t get_position(MarketId market) const {
    int64_t total = 0;
    for (auto b : backends_) {
      std::string pos_json = b->get_positions();
      try {
        auto j = nlohmann::json::parse(pos_json);
        if (j.contains("positions")) {
          for (const auto &p : j["positions"]) {
            std::string ticker;
            if (p.contains("ticker")) ticker = p["ticker"];
            else if (p.contains("market_ticker")) ticker = p["market_ticker"];
            
            if (fnv1a(ticker.c_str()) == market.hash) {
                if (p.contains("size")) {
                    if (p["size"].is_string()) total += std::stoll(p["size"].get<std::string>());
                    else total += p["size"].get<int64_t>();
                } else if (p.contains("quantity")) {
                    total += p["quantity"].get<int64_t>();
                }
            }
          }
        }
      } catch (...) {
      }
    }
    return total;
  }

  virtual Price get_balance() const {
    Price total(0);
    for (auto b : backends_) {
      total = total + b->get_balance();
    }
    return total;
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

  virtual void run() {
    is_running = true;
    std::cout << "[ENGINE] Starting default responsive event loop..." << std::endl;
    while (is_running) {
      GlobalAlgoManager.tick(*this);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  virtual void trigger_tick() {}
};

class LiveExecutionEngine : public ExecutionEngine {
  mutable std::mutex mtx;
  Price cached_balance{0};
  std::unordered_map<uint32_t, int64_t> cached_positions;
  std::thread sync_thread;
  std::condition_variable tick_cv;
  std::mutex tick_mtx;

public:
  LiveExecutionEngine() = default;
  ~LiveExecutionEngine() {
    stop();
    if (sync_thread.joinable()) sync_thread.join();
    tick_cv.notify_all();
  }

  void trigger_tick() override {
    tick_cv.notify_one();
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

  Price get_exposure() const override {
    std::lock_guard<std::mutex> lock(mtx);
    Price total_exposure(0);
    // Note: To calculate real exposure, we'd need to map hashes back to tickers
    // and query live prices. Since we cache positions by hash, we'll iterate
    // and try to find a matching price in our backends.
    for (auto const &[hash, qty] : cached_positions) {
      if (qty == 0) continue;
      // Find current price for this market hash
      for (auto b : backends_) {
        // This is still a bit approximate as we don't store hash->ticker map
        // globally yet. In a production app, we'd store the ticker string
        // during sync_state.
      }
    }
    return total_exposure;
  }

  Price get_pnl() const override {
    // Requires tracking entry prices (cost basis), which would be done
    // in add_order_fill.
    return Price(0);
  }

  void run() override {
    is_running = true;

    // Perform initial synchronous sync to ensure balance/positions are available immediately
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
        // Wait for market update OR 100ms timeout (for time-based algos like TWAP)
        tick_cv.wait_for(lock, std::chrono::milliseconds(100));
      }

      if (!is_running) break;

      GlobalAlgoManager.tick(*this);
      check_kill_switch();
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
      db.log_pnl_snapshot(cached_balance, get_pnl(), current_daily_pnl_raw.load());
    }
    std::cout << "[LIVE ENGINE] Synced state: Balance=" << cached_balance << " Markets=" << cached_positions.size() << std::endl;
  }
};

} // namespace bop

extern bop::ExecutionEngine &LiveExchange;

#include "algo_manager.hpp"

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &engine) {
  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t latency = now - o.creation_timestamp_ns;

  if (!engine.check_risk(o)) {
    std::cout << "[ENGINE] Order rejected by risk engine." << std::endl;
    return;
  }

  if (o.algo_type != bop::AlgoType::None) {
    std::cout << "[ALGO] Registering " << (int)o.algo_type << " for "
              << o.market.ticker << std::endl;
    bop::GlobalAlgoManager.submit(o);
    return;
  }

  if (o.backend) {
    std::string id;
    if (o.is_spread) {
      std::cout << "[BACKEND] Dispatching spread order (" << o.market.hash
                << " - " << o.market2.hash << ") to " << o.backend->name()
                << " (" << latency << " ns latency)" << std::endl;
      id = o.backend->create_order(o);
    } else {
      std::cout << "[BACKEND] Dispatching to " << o.backend->name() << " ("
                << latency << " ns latency)" << std::endl;
      id = o.backend->create_order(o);
    }
    
    if (!id.empty() && id != "error") {
      engine.track_order(id, o);
    }
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
  } else if constexpr (std::is_same_v<Tag, OpenOrdersTag>) {
    size_t val = LiveExchange.get_open_order_count(query.market);
    return is_greater ? val > (size_t)threshold : val < (size_t)threshold;
  }
  return false;
}

} // namespace bop

template <typename T>
class PersistentConditionalStrategy : public ExecutionStrategy {
  ConditionalOrder<T> co;

public:
  PersistentConditionalStrategy(const ConditionalOrder<T> &order) : co(order) {}
  bool tick(ExecutionEngine &engine) override {
    if (co.condition.eval()) {
      std::cout << "[STRATEGY] Condition met! Triggering order..." << std::endl;
      co.order >> engine;
      return true; // Strategy completed
    }
    return false; // Continue monitoring
  }
};

template <typename T>
inline void operator>>(const bop::ConditionalOrder<T> &co,
                       bop::ExecutionEngine &engine) {
  std::cout << "[STRATEGY] Registering persistent conditional order..."
            << std::endl;
  bop::GlobalAlgoManager.submit_strategy(
      std::make_unique<bop::PersistentConditionalStrategy<T>>(co));
}

inline void operator>>(const bop::OCOOrder &oco, bop::ExecutionEngine &engine) {
  std::cout << "[OCO] Dispatching OCO pair..." << std::endl;
  oco.order1 >> engine;
  oco.order2 >> engine;
}
