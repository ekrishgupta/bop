#pragma once

#include "core.hpp"
#include "logic.hpp"
#include "order_tracker.hpp"
#include "streaming_backend.hpp"
#include "database.hpp"
#include "greek_engine.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <initializer_list>
#include <type_traits>
#include <variant>
#include <deque>

namespace bop {

struct Command {
    enum class Type { SubmitOrder, CancelOrder, BatchSubmit };
    Type type;
    std::variant<std::monostate, Order, std::string, std::vector<Order>> data;
};

struct RiskLimits {
  int64_t max_position_size = 10000;
  Price max_market_exposure = Price::from_usd(5000);
  Price max_sector_exposure = Price::from_usd(15000);
  double fat_finger_threshold = 0.10; // 10% deviation from BBO
  Price daily_loss_limit = Price::from_usd(1000);

  // Portfolio-level metrics
  double max_leverage = 3.0;
  double max_drawdown_percent = 0.10; // 10% from peak
  double max_correlation_threshold = 0.85;

  // Dynamic Position Sizing
  bool dynamic_sizing_enabled = false;
  double risk_per_trade_percent = 0.02; // Risk 2% of equity per trade
  int64_t min_order_quantity = 1;

  // Circuit Breakers
  double volatility_threshold = 0.50; // 50% spike in rolling vol
  bool circuit_breakers_enabled = true;
};

struct VolatilityTracker {
    std::deque<double> returns;
    size_t window_size = 20;
    double current_vol = 0.0;

    void add_price(Price p) {
        static Price last_p = Price(0);
        if (last_p.raw > 0) {
            double ret = std::abs(p.to_double() - last_p.to_double()) / last_p.to_double();
            returns.push_back(ret);
            if (returns.size() > window_size) returns.pop_front();
            
            // Calculate Std Dev
            double sum = 0;
            for (double r : returns) sum += r;
            double mean = sum / returns.size();
            double sq_sum = 0;
            for (double r : returns) sq_sum += (r - mean) * (r - mean);
            current_vol = std::sqrt(sq_sum / returns.size());
        }
        last_p = p;
    }
};

struct ExecutionEngine {
  std::atomic<bool> is_running{false};
  mutable std::vector<const MarketBackend *> backends_;
  OrderTracker order_store;
  RiskLimits limits;
  std::atomic<int64_t> current_daily_pnl_raw{0};
  std::unordered_map<std::string, std::string> market_to_sector;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, double>> correlations;
  std::unordered_map<uint32_t, VolatilityTracker> market_volatility;
  std::atomic<bool> circuit_breaker_active{false};
  GreekEngine greek_engine;
  mutable std::mutex risk_mtx;
  Database db;
  std::atomic<int64_t> last_tick_time_ns{0};

  mutable std::mutex cmd_mtx;
  std::deque<Command> command_queue_;

  virtual ~ExecutionEngine() = default;

  void submit_command(Command cmd) {
    std::lock_guard<std::mutex> lock(cmd_mtx);
    command_queue_.push_back(std::move(cmd));
  }

  void process_commands() {
    std::deque<Command> to_process;
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        if (command_queue_.empty()) return;
        to_process = std::move(command_queue_);
    }

    for (const auto& cmd : to_process) {
        if (cmd.type == Command::Type::SubmitOrder) {
            execute_order(std::get<Order>(cmd.data));
        } else if (cmd.type == Command::Type::CancelOrder) {
            execute_cancel(std::get<std::string>(cmd.data));
        } else if (cmd.type == Command::Type::BatchSubmit) {
            execute_batch(std::get<std::vector<Order>>(cmd.data));
        }
    }
  }

  virtual void execute_order(const Order &o);
  virtual void execute_cancel(const std::string &id);
  virtual void execute_batch(const std::vector<Order> &orders);

  void set_sector(const std::string &ticker, const std::string &sector) {
    market_to_sector[ticker] = sector;
  }

  void set_correlation(const std::string &m1, const std::string &m2, double val) {
    uint32_t h1 = fnv1a(m1.c_str());
    uint32_t h2 = fnv1a(m2.c_str());
    correlations[h1][h2] = val;
    correlations[h2][h1] = val;
  }

  bool check_correlation_risk(const Order &o) const {
    uint32_t target_hash = o.market.hash;
    auto it = correlations.find(target_hash);
    if (it == correlations.end()) return true;

    for (auto const& [other_hash, corr] : it->second) {
      if (std::abs(corr) > limits.max_correlation_threshold) {
        int64_t other_pos = get_position(MarketId(other_hash));
        if (other_pos != 0) {
            bool same_dir = (corr > 0 && ((o.is_buy && other_pos > 0) || (!o.is_buy && other_pos < 0))) ||
                           (corr < 0 && ((o.is_buy && other_pos < 0) || (!o.is_buy && other_pos > 0)));
            if (same_dir) {
                std::cerr << "[RISK] REJECT: High correlation (" << corr 
                          << ") with existing position in market hash " << other_hash << std::endl;
                return false;
            }
        }
      }
    }
    return true;
  }

  int64_t calculate_dynamic_size(const Order &o) const {
    if (!limits.dynamic_sizing_enabled) return o.quantity;

    Price equity = get_balance();
    if (equity.raw <= 0) return limits.min_order_quantity;

    double risk_amount = equity.to_double() * limits.risk_per_trade_percent;
    Price p = (o.price.raw > 0) ? o.price : get_price(o.market, o.outcome_yes);
    if (p.raw == 0) p = Price::from_usd(0.5);

    int64_t size = static_cast<int64_t>(risk_amount / p.to_double());
    if (size > limits.max_position_size) size = limits.max_position_size;
    if (size < limits.min_order_quantity) size = limits.min_order_quantity;
    return size;
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
  void track_order(const std::string &id, const Order &o);
  void update_order_status(const std::string &id, OrderStatus status);
  void add_order_fill(const std::string &id, int qty, Price price);

  // Risk Management
  bool check_risk(const Order &o) const {
    std::lock_guard<std::mutex> lock(risk_mtx);

    // 0. Kill-Switch Check
    if (current_daily_pnl_raw.load() <= -limits.daily_loss_limit.raw) {
      std::cerr << "[RISK] REJECT: Kill-switch is ACTIVE. Daily loss: "
                << Price(current_daily_pnl_raw.load()) << std::endl;
      return false;
    }

    // 0.05 Circuit Breaker
    if (circuit_breaker_active.load()) {
        std::cerr << "[RISK] REJECT: Circuit breaker is ACTIVE due to high volatility." << std::endl;
        return false;
    }

    // 0.1 Leverage Check
    Price balance = get_balance();
    if (balance.raw > 0) {
        Price exposure = get_exposure();
        double leverage = exposure.to_double() / balance.to_double();
        if (leverage > limits.max_leverage) {
            std::cerr << "[RISK] REJECT: Portfolio leverage too high: " << leverage << std::endl;
            return false;
        }
    }

    // 0.2 Correlation Check
    if (!check_correlation_risk(o)) return false;

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

  virtual double get_portfolio_metric(PortfolioQuery::Metric metric) const {
    return 0.0;
  }

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
    return 0; 
  }

  virtual void run();
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
  ~LiveExecutionEngine();

  void trigger_tick() override {
    tick_cv.notify_one();
  }

  int64_t get_position(MarketId market) const override;
  Price get_balance() const override;
  Price get_exposure() const override;
  Price get_pnl() const override;
  double get_portfolio_metric(PortfolioQuery::Metric metric) const override;
  void run() override;

  const std::unordered_map<uint32_t, int64_t>& get_positions_map() const {
      return cached_positions;
  }

private:
  void sync_state();
};

} // namespace bop

extern bop::ExecutionEngine &LiveExchange;

#include "algo_manager.hpp"

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
    return false;
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
  } else if constexpr (std::is_same_v<Tag, PortfolioTag>) {
    double val = LiveExchange.get_portfolio_metric(query.metric);
    int64_t scaled_val = static_cast<int64_t>(val * 1000000);
    return is_greater ? scaled_val > threshold : scaled_val < threshold;
  }
  return false;
}

template <typename T>
class PersistentConditionalStrategy : public bop::ExecutionStrategy {
  bop::ConditionalOrder<T> co;

public:
  PersistentConditionalStrategy(const bop::ConditionalOrder<T> &order) : co(order) {}
  bool tick(ExecutionEngine &engine) override;
};

template <typename T>
inline void operator>>(const bop::ConditionalOrder<T> &co,
                       bop::ExecutionEngine &engine) {
  std::cout << "[STRATEGY] Registering persistent conditional order..."
            << std::endl;
  bop::GlobalAlgoManager.submit_strategy(
      std::make_unique<PersistentConditionalStrategy<T>>(co));
}

// Forward declare restored Operators
void operator>>(const Order &o, ExecutionEngine &engine);
void operator>>(std::initializer_list<Order> batch, ExecutionEngine &engine);
void operator>>(const OCOOrder &oco, ExecutionEngine &engine);

} // namespace bop
