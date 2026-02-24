#pragma once

#include "auth.hpp"
#include "core.hpp"
#include "nlohmann/json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace bop {

// Supporting Data Structures for Exhaustive API Coverage
struct OrderBookLevel {
  Price price;
  int64_t quantity;
  std::string order_id; // For L3/Incremental updates
};

struct SuperMarket {
    std::string ticker;
    struct Entry {
        MarketId market;
        const MarketBackend* backend;
    };
    std::vector<Entry> entries;
};

class MarketRegistry {
public:
    static void Register(const std::string& super_ticker, MarketId mkt, const MarketBackend& backend) {
        instance().markets[super_ticker].ticker = super_ticker;
        instance().markets[super_ticker].entries.push_back({mkt, &backend});
    }

    static const SuperMarket* Get(const std::string& super_ticker) {
        auto it = instance().markets.find(super_ticker);
        if (it != instance().markets.end()) return &it->second;
        return nullptr;
    }

private:
    static MarketRegistry& instance() {
        static MarketRegistry reg;
        return reg;
    }
    std::unordered_map<std::string, SuperMarket> markets;
};

struct OrderBook {
  std::vector<OrderBookLevel> bids;
  std::vector<OrderBookLevel> asks;
  int64_t last_update_id = 0; // Sequence number for incremental updates
};

struct Candlestick {
  int64_t timestamp;
  Price open;
  Price high;
  Price low;
  Price close;
  int64_t volume;
};

struct PortfolioSummary {
  Price balance;
  Price initial_margin;
  Price maintenance_margin;
  Price portfolio_value;
};

struct MarketBackend {
  virtual ~MarketBackend() = default;
  virtual std::string name() const = 0;

  auth::Credentials credentials;
  std::map<std::string, std::string> ticker_to_id;

  void set_credentials(const auth::Credentials &creds) { credentials = creds; }

  virtual void sync_markets() {}

  virtual std::string resolve_ticker(const std::string &ticker) const {
    if (ticker_to_id.empty()) {
        const_cast<MarketBackend *>(this)->sync_markets();
    }

    // 1. Exact match
    auto it = ticker_to_id.find(ticker);
    if (it != ticker_to_id.end()) {
      return it->second;
    }

    // 2. Case-insensitive search
    std::string upper_ticker = ticker;
    for (auto &c : upper_ticker) c = toupper(c);
    
    for (auto const& [key, val] : ticker_to_id) {
        std::string upper_key = key;
        for (auto &c : upper_key) c = toupper(c);
        if (upper_key == upper_ticker) return val;
    }

    return ticker;
  }

  // --- Exchange & Status ---
  virtual std::string get_exchange_status() const { return "active"; }
  virtual std::string get_exchange_schedule() const { return "24/7"; }
  virtual std::vector<std::string> get_exchange_announcements() const {
    return {};
  }
  virtual std::string get_series_fee_changes() const { return "none"; }
  virtual int64_t clob_get_server_time() const { return 0; }

  // --- Market Data (Live) ---
  virtual Price get_price(MarketId market, bool outcome_yes) const = 0;
  virtual Price get_depth(MarketId market, bool is_bid) const = 0;
  virtual OrderBook get_orderbook(MarketId market) const { return {}; }
  virtual int64_t get_market_expiry(MarketId market) const { return 0; }
  virtual std::vector<Candlestick> get_candlesticks(MarketId market) const {
    return {};
  }
  virtual std::string get_market_details(MarketId market) const { return ""; }
  virtual std::vector<std::string> list_markets() const { return {}; }
  virtual std::vector<std::string> list_events() const { return {}; }
  virtual std::vector<std::string> list_series() const { return {}; }

  // Polymarket Gamma & CLOB Specifics
  virtual std::string gamma_get_event(const std::string &id) const {
    return "";
  }
  virtual std::string gamma_get_market(const std::string &id) const {
    return "";
  }
  virtual Price clob_get_midpoint(MarketId market) const { return Price(0); }
  virtual Price clob_get_spread(MarketId market) const { return Price(0); }
  virtual Price clob_get_last_trade_price(MarketId market) const {
    return Price(0);
  }
  virtual double clob_get_fee_rate(MarketId market) const { return 0.0; }
  virtual Price clob_get_tick_size(MarketId market) const { return Price(0); }

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
  virtual Price get_balance() const { return Price(0); }
  virtual PortfolioSummary get_portfolio_summary() const {
    return {Price(0), Price(0), Price(0), Price(0)};
  }
  virtual std::string get_positions() const { return ""; }
  virtual std::string get_fills() const { return ""; }
  virtual std::string get_settlements() const { return ""; }

  virtual std::string create_subaccount(const std::string &name) const {
    return "";
  }
  virtual bool transfer_funds(const std::string &from, const std::string &to,
                              Price amount) const {
    return true;
  }
  virtual std::string get_subaccount_balances() const { return ""; }

  // --- Account Management ---
  virtual std::string get_profile() const { return ""; }
  virtual std::vector<std::string> list_api_keys() const { return {}; }
  virtual bool create_api_key() const { return true; }
  virtual bool delete_api_key(const std::string &key_id) const { return true; }
  virtual bool create_withdrawal(Price amount,
                                 const std::string &method) const {
    return true;
  }
  virtual bool create_deposit(Price amount, const std::string &method) const {
    return true;
  }

  // --- WebSocket Streaming ---
  virtual void ws_subscribe_orderbook(
      MarketId market, std::function<void(const OrderBook &)> callback) const {}
  virtual void
  ws_subscribe_trades(MarketId market,
                      std::function<void(bop::Price, int64_t)> callback) const {
  }
  virtual void ws_unsubscribe(MarketId market) const {}
};

} // namespace bop
