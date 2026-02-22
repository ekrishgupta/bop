#pragma once

#include "core.hpp"
#include <optional>
#include <string>
#include <vector>

namespace bop {

// Supporting Data Structures for Exhaustive API Coverage
struct OrderBookLevel {
  int64_t price;
  int64_t quantity;
};

struct OrderBook {
  std::vector<OrderBookLevel> bids;
  std::vector<OrderBookLevel> asks;
};

struct Candlestick {
  int64_t timestamp;
  int64_t open;
  int64_t high;
  int64_t low;
  int64_t close;
  int64_t volume;
};

struct PortfolioSummary {
  int64_t balance;
  int64_t initial_margin;
  int64_t maintenance_margin;
  int64_t portfolio_value;
};

struct MarketBackend {
  virtual ~MarketBackend() = default;
  virtual std::string name() const = 0;

  // --- Exchange & Status ---
  virtual std::string get_exchange_status() const { return "active"; }
  virtual std::string get_exchange_schedule() const { return "24/7"; }
  virtual std::vector<std::string> get_exchange_announcements() const {
    return {};
  }
  virtual std::string get_series_fee_changes() const { return "none"; }

  // --- Market Data (Live) ---
  virtual int64_t get_price(MarketId market, bool outcome_yes) const = 0;
  virtual int64_t get_depth(MarketId market, bool is_bid) const = 0;
  virtual OrderBook get_orderbook(MarketId market) const { return {}; }
  virtual std::vector<Candlestick> get_candlesticks(MarketId market) const {
    return {};
  }
  virtual std::string get_market_details(MarketId market) const { return ""; }
  virtual std::vector<std::string> list_markets() const { return {}; }
  virtual std::vector<std::string> list_events() const { return {}; }
  virtual std::vector<std::string> list_series() const { return {}; }

  // --- Historical Data ---
  virtual std::string get_historical_cutoff() const { return ""; }
  virtual std::vector<std::string> get_historical_markets() const { return {}; }
  virtual std::vector<Candlestick>
  get_historical_candlesticks(MarketId market) const {
    return {};
  }
  virtual std::string get_historical_fills() const { return ""; }
  virtual std::string get_historical_orders() const { return ""; }

  // --- Trading ---
  virtual std::string create_order(const Order &order) const { return "id"; }
  virtual bool cancel_order(const std::string &order_id) const { return true; }
  virtual bool amend_order(const std::string &order_id,
                           int quantity_reduction) const {
    return true;
  }
  virtual std::vector<std::string>
  create_batch_orders(const std::vector<Order> &orders) const {
    return {};
  }
  virtual bool
  cancel_batch_orders(const std::vector<std::string> &order_ids) const {
    return true;
  }

  // --- Portfolio & Subaccounts ---
  virtual int64_t get_balance() const { return 0; }
  virtual PortfolioSummary get_portfolio_summary() const {
    return {0, 0, 0, 0};
  }
  virtual std::string get_positions() const { return ""; }
  virtual std::string get_fills() const { return ""; }
  virtual std::string get_settlements() const { return ""; }

  virtual std::string create_subaccount(const std::string &name) const {
    return "";
  }
  virtual bool transfer_funds(const std::string &from, const std::string &to,
                              int64_t amount) const {
    return true;
  }
  virtual std::string get_subaccount_balances() const { return ""; }

  // --- Account Management ---
  virtual std::string get_profile() const { return ""; }
  virtual std::vector<std::string> list_api_keys() const { return {}; }
  virtual bool create_api_key() const { return true; }
  virtual bool delete_api_key(const std::string &key_id) const { return true; }
  virtual bool create_withdrawal(int64_t amount,
                                 const std::string &method) const {
    return true;
  }
  virtual bool create_deposit(int64_t amount, const std::string &method) const {
    return true;
  }
};

} // namespace bop
