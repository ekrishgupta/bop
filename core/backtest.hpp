#pragma once

#include "engine.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace bop {

struct LatencyModel {
  int64_t mean_latency_ns = 5000000; // 5ms
  int64_t std_dev_ns = 1000000;      // 1ms
};

struct SlippageModel {
  double fixed_bps = 0.0;
  double vol_multiplier = 0.0;
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
      for (const auto& [hash, qty] : positions_) {
          if (qty == 0) continue;
          nlohmann::json p;
          auto it = hash_to_ticker_.find(hash);
          p["ticker"] = (it != hash_to_ticker_.end()) ? it->second : std::to_string(hash);
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
    int64_t latency = latency_model_.mean_latency_ns;
    tracked_order.creation_timestamp_ns = current_time_ns_ + latency;
    
    std::cout << "[BACKTEST] Created order " << id << " for " << order.market.ticker << " at " << current_time_ns_ << " visible at " << tracked_order.creation_timestamp_ns << std::endl;
    pending_orders_[id] = tracked_order;
    return id;
  }

  bool cancel_order(const std::string &order_id) const override {
    return pending_orders_.erase(order_id) > 0;
  }

  void set_current_time(int64_t ns) { current_time_ns_ = ns; }

  void match_orders(ExecutionEngine* engine) {
    auto it = pending_orders_.begin();
    while (it != pending_orders_.end()) {
      const auto &id = it->first;
      const auto &order = it->second;
      
      std::cout << "[BACKTEST] Checking match for " << id << ": time=" << current_time_ns_ << " visible_at=" << order.creation_timestamp_ns << std::endl;

      if (current_time_ns_ < order.creation_timestamp_ns) {
        ++it;
        continue;
      }

      Price current_price = get_price(order.market, order.outcome_yes);
      
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
        
        if (slippage_model_.fixed_bps > 0) {
            double slippage = slippage_model_.fixed_bps / 10000.0;
            if (order.is_buy) fill_price = Price::from_double(fill_price.to_double() * (1.0 + slippage));
            else fill_price = Price::from_double(fill_price.to_double() * (1.0 - slippage));
        }

        int qty = order.quantity;
        
        if (order.is_buy) {
            positions_[order.market.hash] += qty;
            cached_balance_ = cached_balance_ - Price(fill_price.raw * qty / Price::SCALE);
        } else {
            positions_[order.market.hash] -= qty;
            cached_balance_ = cached_balance_ + Price(fill_price.raw * qty / Price::SCALE);
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
  BacktestExecutionEngine() {
    is_running = false;
  }

  void set_current_time(int64_t ns) {
    current_time_ns_ = ns;
    for (auto b : backends_) {
      if (auto bb = dynamic_cast<BacktestMarketBackend*>(const_cast<MarketBackend*>(b))) {
        bb->set_current_time(ns);
      }
    }
  }

  void run_from_csv(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
      std::cerr << "[BACKTEST] Failed to open file: " << filename << std::endl;
      return;
    }

    std::cout << "[BACKTEST] Processing historical data from " << filename << "..." << std::endl;
    is_running = true;

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line) && is_running) {
      std::stringstream ss(line);
      std::string timestamp_str, ticker, yes_price_str, no_price_str;
      
      std::getline(ss, timestamp_str, ',');
      std::getline(ss, ticker, ',');
      std::getline(ss, yes_price_str, ',');
      std::getline(ss, no_price_str, ',');

      if (ticker.empty()) continue;

      int64_t ts = std::stoll(timestamp_str);
      set_current_time(ts * 1000000000LL);

      Price yes_p = Price::from_double(std::stod(yes_price_str));
      Price no_p = Price::from_double(std::stod(no_price_str));
      
      update_market(ticker, yes_p, no_p);
      GlobalAlgoManager.tick(*this);
      
      for (auto b : backends_) {
        if (auto bb = dynamic_cast<BacktestMarketBackend*>(const_cast<MarketBackend*>(b))) {
          bb->match_orders(this);
        }
      }
    }
    
    is_running = false;
    std::cout << "[BACKTEST] Simulation complete." << std::endl;
  }

  void run_from_json(const std::string &filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    is_running = true;
    try {
        nlohmann::json data = nlohmann::json::parse(file);
        for (const auto& entry : data) {
            if (!is_running) break;
            std::string ticker = entry["ticker"];
            Price yes_p = Price::from_double(entry["yes_price"].get<double>());
            Price no_p = Price::from_double(entry["no_price"].get<double>());
            update_market(ticker, yes_p, no_p);
            GlobalAlgoManager.tick(*this);
            for (auto b : backends_) {
                if (auto bb = dynamic_cast<BacktestMarketBackend*>(const_cast<MarketBackend*>(b))) {
                    bb->match_orders(this);
                } else {
                    std::cout << "[BACKTEST] Found non-backtest backend: " << b->name() << std::endl;
                }
            }
        }
    } catch (...) {}
    is_running = false;
  }

  void update_market(const std::string &ticker, Price yes, Price no) {
    MarketId mkt(ticker.c_str());
    for (auto b : backends_) {
      if (auto bb = dynamic_cast<BacktestMarketBackend*>(const_cast<MarketBackend*>(b))) {
        bb->set_price(mkt, yes, no);
      }
    }
  }

  int64_t get_position(MarketId market) const override {
    return ExecutionEngine::get_position(market);
  }

  Price get_balance() const override {
    Price total(0);
    for (auto b : backends_) {
      total = total + b->get_balance();
    }
    return total;
  }

  void report() const {
    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "       BACKTEST PERFORMANCE REPORT\n";
    std::cout << std::string(40, '=') << "\n";
    
    Price total_balance = get_balance();
    std::cout << "Final Balance:    " << total_balance << "\n";
    
    // In a real system, we'd calculate drawdown, Sharpe ratio, etc.
    // For now, let's list the positions.
    std::cout << "Positions:\n";
    for (auto b : backends_) {
        std::cout << "  - " << b->name() << ": " << b->get_positions() << "\n";
    }
    std::cout << std::string(40, '=') << "\n";
  }

private:
  int64_t current_time_ns_ = 0;
};

} // namespace bop
