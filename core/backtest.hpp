#pragma once

#include "engine.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>

namespace bop {

struct LatencyModel {
  int64_t mean_latency_ns = 5000000; // 5ms
  int64_t std_dev_ns = 1000000;      // 1ms
};

struct SlippageModel {
  double fixed_bps = 0.0;
  double vol_multiplier = 0.0;
  double impact_constant = 0.0; // Price impact per share

  // Square-root law parameters
  bool use_sqrt_law = false;
  double sigma = 0.02;           // Daily volatility (e.g., 0.02 for 2%)
  double daily_volume = 1000000; // Estimated daily volume for the market
};

struct BacktestStats {
  std::vector<double> equity_curve;
  std::vector<int64_t> timestamps;
  double max_drawdown = 0.0;
  double sharpe_ratio = 0.0;
  int total_trades = 0;
};

/**
 * @brief A backend for backtesting that simulates market behavior.
 */
struct BacktestMarketBackend : public MarketBackend {
  explicit BacktestMarketBackend(const std::string &name) : name_(name) {
    cached_balance_ = Price::from_usd(10000); // Default backtest balance
  }

  std::string name() const override { return name_; }

  void set_latency_model(LatencyModel model) { latency_model_ = model; }
  void set_slippage_model(SlippageModel model) { slippage_model_ = model; }

  Price get_price(MarketId market, bool outcome_yes) const override {
    auto it = prices_.find(market.hash);
    if (it != prices_.end()) {
      return outcome_yes ? it->second.first : it->second.second;
    }
    return Price(0);
  }

  Price get_depth(MarketId market, bool is_bid) const override {
    return get_price(market, is_bid);
  }

  Price get_balance() const override { return cached_balance_; }

  std::string get_positions() const override {
    nlohmann::json j;
    j["positions"] = nlohmann::json::array();
    for (const auto &[hash, qty] : positions_) {
      if (qty == 0)
        continue;
      nlohmann::json p;
      auto it = hash_to_ticker_.find(hash);
      p["ticker"] =
          (it != hash_to_ticker_.end()) ? it->second : std::to_string(hash);
      p["size"] = qty;
      j["positions"].push_back(p);
    }
    return j.dump();
  }

  void set_price(MarketId market, Price yes, Price no) {
    prices_[market.hash] = {yes, no};
    if (!market.ticker.empty()) {
      hash_to_ticker_[market.hash] = market.ticker;
    }
  }

  void set_balance(Price balance) { cached_balance_ = balance; }

  std::string create_order(const Order &order) const override {
    static int next_id = 1;
    std::string id = "backtest_" + std::to_string(next_id++);

    Order tracked_order = order;

    // Simulate latency with a normal distribution
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::normal_distribution<> d(
        static_cast<double>(latency_model_.mean_latency_ns),
        static_cast<double>(latency_model_.std_dev_ns));

    int64_t latency = static_cast<int64_t>(std::max(0.0, d(gen)));
    tracked_order.creation_timestamp_ns = current_time_ns_ + latency;

    pending_orders_[id] = tracked_order;
    return id;
  }

  bool cancel_order(const std::string &order_id) const override {
    return pending_orders_.erase(order_id) > 0;
  }

  void set_current_time(int64_t ns) { current_time_ns_ = ns; }

  void match_orders(ExecutionEngine *engine) {
    auto it = pending_orders_.begin();
    while (it != pending_orders_.end()) {
      const auto &id = it->first;
      const auto &order = it->second;

      if (current_time_ns_ < order.creation_timestamp_ns) {
        ++it;
        continue;
      }

      Price current_price = get_price(order.market, order.outcome_yes);
      if (current_price.raw == 0) {
        ++it;
        continue;
      }

      bool filled = false;
      if (order.is_buy) {
        if (order.price.raw == 0 || current_price <= order.price) {
          filled = true;
        }
      } else {
        if (order.price.raw == 0 || current_price >= order.price) {
          filled = true;
        }
      }

      if (filled) {
        Price fill_price = order.price.raw == 0 ? current_price : order.price;

        // Apply slippage
        if (slippage_model_.fixed_bps > 0) {
          double slippage = slippage_model_.fixed_bps / 10000.0;
          if (order.is_buy)
            fill_price =
                Price::from_double(fill_price.to_double() * (1.0 + slippage));
          else
            fill_price =
                Price::from_double(fill_price.to_double() * (1.0 - slippage));
        }

        // Apply square-root law price impact
        if (slippage_model_.use_sqrt_law && slippage_model_.daily_volume > 0) {
          double q = static_cast<double>(order.quantity);
          double v = slippage_model_.daily_volume;
          double s = slippage_model_.sigma;

          // Current volatility estimate (could be improved with historical
          // lookback)
          double impact_pct = s * std::sqrt(q / v);

          if (order.is_buy)
            fill_price =
                Price::from_double(fill_price.to_double() * (1.0 + impact_pct));
          else
            fill_price =
                Price::from_double(fill_price.to_double() * (1.0 - impact_pct));
        }

        // Apply fallback price impact per share if square-root law is not used
        // or in addition
        if (!slippage_model_.use_sqrt_law &&
            slippage_model_.impact_constant > 0) {
          double impact = slippage_model_.impact_constant * order.quantity;
          if (order.is_buy)
            fill_price.raw += static_cast<int64_t>(impact * 100);
          else
            fill_price.raw -= static_cast<int64_t>(impact * 100);
        }

        int qty = order.quantity;
        if (order.is_buy) {
          positions_[order.market.hash] += qty;
          cached_balance_ =
              cached_balance_ - Price(fill_price.raw * qty / Price::SCALE);
        } else {
          positions_[order.market.hash] -= qty;
          cached_balance_ =
              cached_balance_ + Price(fill_price.raw * qty / Price::SCALE);
        }

        engine->add_order_fill(id, qty, fill_price);
        it = pending_orders_.erase(it);
      } else {
        ++it;
      }
    }
  }

private:
  std::string name_;
  mutable Price cached_balance_;
  mutable int64_t current_time_ns_ = 0;
  LatencyModel latency_model_;
  SlippageModel slippage_model_;
  mutable std::map<uint32_t, std::pair<Price, Price>> prices_;
  mutable std::map<uint32_t, std::string> hash_to_ticker_;
  mutable std::map<uint32_t, int64_t> positions_;
  mutable std::map<std::string, Order> pending_orders_;
};

/**
 * @brief Execution engine optimized for historical backtesting.
 */
class BacktestExecutionEngine : public ExecutionEngine {
public:
  BacktestExecutionEngine() { is_running = false; }

  void set_current_time(int64_t ns) {
    current_time_ns_ = ns;
    for (auto b : backends_) {
      if (auto bb = dynamic_cast<BacktestMarketBackend *>(
              const_cast<MarketBackend *>(b))) {
        bb->set_current_time(ns);
      }
    }
  }

  void run_from_csv(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open())
      return;

    is_running = true;
    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line) && is_running) {
      std::stringstream ss(line);
      std::string ts_s, ticker, yes_s, no_s;
      std::getline(ss, ts_s, ',');
      std::getline(ss, ticker, ',');
      std::getline(ss, yes_s, ',');
      std::getline(ss, no_s, ',');

      if (ticker.empty())
        continue;

      int64_t ts = std::stoll(ts_s);
      set_current_time(ts * 1000000000LL);

      Price yes_p = Price::from_double(std::stod(yes_s));
      Price no_p = Price::from_double(std::stod(no_s));

      update_market(ticker, yes_p, no_p);
      GlobalAlgoManager.tick(*this);

      for (auto b : backends_) {
        if (auto bb = dynamic_cast<BacktestMarketBackend *>(
                const_cast<MarketBackend *>(b))) {
          bb->match_orders(this);
        }
      }
      record_snapshot(ts);
    }
    is_running = false;
    calculate_stats();
  }

  void run_from_json(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open())
      return;

    is_running = true;
    try {
      nlohmann::json data = nlohmann::json::parse(file);
      for (const auto &entry : data) {
        if (!is_running)
          break;
        std::string ticker = entry["ticker"];
        int64_t ts = entry["timestamp"];
        set_current_time(ts * 1000000000LL);

        Price yes_p = Price::from_double(entry["yes_price"].get<double>());
        Price no_p = Price::from_double(entry["no_price"].get<double>());
        update_market(ticker, yes_p, no_p);
        GlobalAlgoManager.tick(*this);
        for (auto b : backends_) {
          if (auto bb = dynamic_cast<BacktestMarketBackend *>(
                  const_cast<MarketBackend *>(b))) {
            bb->match_orders(this);
          }
        }
        record_snapshot(ts);
      }
    } catch (...) {
    }
    is_running = false;
    calculate_stats();
  }

  void update_market(const std::string &ticker, Price yes, Price no) {
    MarketId mkt(ticker.c_str());
    for (auto b : backends_) {
      if (auto bb = dynamic_cast<BacktestMarketBackend *>(
              const_cast<MarketBackend *>(b))) {
        bb->set_price(mkt, yes, no);
      }
    }
  }

  void record_snapshot(int64_t ts) {
    stats_.equity_curve.push_back(get_balance().to_double());
    stats_.timestamps.push_back(ts);
  }

  void calculate_stats() {
    if (stats_.equity_curve.size() < 2)
      return;
    double peak = stats_.equity_curve[0];
    double max_dd = 0.0;
    std::vector<double> returns;
    for (size_t i = 1; i < stats_.equity_curve.size(); ++i) {
      double val = stats_.equity_curve[i];
      if (val > peak)
        peak = val;
      double dd = (peak - val) / peak;
      if (dd > max_dd)
        max_dd = dd;
      double ret =
          (val - stats_.equity_curve[i - 1]) / stats_.equity_curve[i - 1];
      returns.push_back(ret);
    }
    stats_.max_drawdown = max_dd;
    if (!returns.empty()) {
      double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
      double mean = sum / returns.size();
      double sq_sum = std::inner_product(returns.begin(), returns.end(),
                                         returns.begin(), 0.0);
      double stdev = std::sqrt(sq_sum / returns.size() - mean * mean);
      if (stdev > 0)
        stats_.sharpe_ratio = (mean / stdev) * std::sqrt(252 * 24 * 60);
    }
  }

  Price get_balance() const override {
    Price total(0);
    for (auto b : backends_) {
      total = total + b->get_balance();
    }
    return total;
  }

  void report() const {
    std::cout << "\n" << std::string(45, '=') << "\n";
    std::cout << "       BACKTEST PERFORMANCE REPORT\n";
    std::cout << std::string(45, '=') << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Final Balance:    $" << get_balance().to_double() << "\n";
    std::cout << "Max Drawdown:     " << (stats_.max_drawdown * 100.0) << "%\n";
    std::cout << "Sharpe Ratio:     " << stats_.sharpe_ratio << "\n";
    std::cout << "Data Points:      " << stats_.equity_curve.size() << "\n";
    std::cout << std::string(45, '=') << "\n";
  }

private:
  int64_t current_time_ns_ = 0;
  BacktestStats stats_;
};

} // namespace bop
