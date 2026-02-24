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
class BacktestMarketBackend : public MarketBackend {
public:
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
    // Simplified: depth is the same as the price in backtesting unless we have L2 data
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

  // Backtest specific controls
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
    // Add latency: order will only be visible/matchable after this time
    int64_t latency = latency_model_.mean_latency_ns; // Simple for now
    tracked_order.creation_timestamp_ns = current_time_ns_ + latency;
    
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
      
      // Check if order has "arrived" at the exchange
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
        
        // Apply slippage
        if (slippage_model_.fixed_bps > 0) {
            double slippage = slippage_model_.fixed_bps / 10000.0;
            if (order.is_buy) fill_price = Price::from_double(fill_price.to_double() * (1.0 + slippage));
            else fill_price = Price::from_double(fill_price.to_double() * (1.0 - slippage));
        }

        int qty = order.quantity;
        
        // Update local state
        if (order.is_buy) {
            positions_[order.market.hash] += qty;
            cached_balance_ = cached_balance_ - Price(fill_price.raw * qty / Price::SCALE);
        } else {
            positions_[order.market.hash] -= qty;
            cached_balance_ = cached_balance_ + Price(fill_price.raw * qty / Price::SCALE);
        }

        // Notify engine
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
  LatencyModel latency_model_;
  SlippageModel slippage_model_;
};

/**
 * @brief Execution engine optimized for historical backtesting.
 */
class BacktestExecutionEngine : public ExecutionEngine {
public:
  BacktestExecutionEngine() {
    is_running = false;
  }

  void run() override {
    std::cout << "[BACKTEST] Starting historical simulation..." << std::endl;
    is_running = true;
    // In backtest mode, run() might not be used the same way as Live
    // because we want to control the loop externally or via data feeding.
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
    // Skip header if exists
    std::getline(file, line);

    while (std::getline(file, line) && is_running) {
      std::stringstream ss(line);
      std::string timestamp_str, ticker, yes_price_str, no_price_str;
      
      std::getline(ss, timestamp_str, ',');
      std::getline(ss, ticker, ',');
      std::getline(ss, yes_price_str, ',');
      std::getline(ss, no_price_str, ',');

      if (ticker.empty()) continue;

      int64_t ts = std::stoll(timestamp_str);
      set_current_time(ts * 1000000000LL); // Convert seconds to ns

      Price yes_p = Price::from_double(std::stod(yes_price_str));
      Price no_p = Price::from_double(std::stod(no_price_str));
      
      update_market(ticker, yes_p, no_p);
      
      // Step the simulation
      GlobalAlgoManager.tick(*this);
      
      // Match orders against new prices
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
    if (!file.is_open()) {
      std::cerr << "[BACKTEST] Failed to open file: " << filename << std::endl;
      return;
    }

    std::cout << "[BACKTEST] Processing historical data from " << filename << "..." << std::endl;
    is_running = true;

    try {
        nlohmann::json data = nlohmann::json::parse(file);
        for (const auto& entry : data) {
            if (!is_running) break;

            if (entry.contains("timestamp")) {
                set_current_time(entry["timestamp"].get<int64_t>() * 1000000000LL);
            }

            std::string ticker = entry["ticker"];
            Price yes_p = Price::from_double(entry["yes_price"].get<double>());
            Price no_p = Price::from_double(entry["no_price"].get<double>());

            update_market(ticker, yes_p, no_p);
            
            GlobalAlgoManager.tick(*this);
            
            for (auto b : backends_) {
                if (auto bb = dynamic_cast<BacktestMarketBackend*>(const_cast<MarketBackend*>(b))) {
                    bb->match_orders(this);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[BACKTEST] JSON error: " << e.what() << std::endl;
    }

    is_running = false;
    std::cout << "[BACKTEST] Simulation complete." << std::endl;
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
    for (auto b : backends_) {
        if (auto bb = dynamic_cast<const BacktestMarketBackend*>(b)) {
            // This is a bit of a hack since BacktestMarketBackend positions_ is private
            // In a real implementation, we'd have a better way to query this.
            // For now, let's use the JSON parsing inherited from ExecutionEngine or 
            // implement a direct way.
        }
    }
    // Fallback to base class logic which parses JSON from get_positions()
    return ExecutionEngine::get_position(market);
  }

  Price get_balance() const override {
    Price total(0);
    for (auto b : backends_) {
      total = total + b->get_balance();
    }
    return total;
  }

private:
  int64_t current_time_ns_ = 0;
};

} // namespace bop
