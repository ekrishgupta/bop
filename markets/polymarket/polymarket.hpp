#pragma once

#include "../../core/market_base.hpp"

namespace bop::markets {

struct Polymarket : public MarketBackend {
  std::string name() const override { return "Polymarket"; }

  // --- Exchange Status & Metadata ---
  int64_t clob_get_server_time() const override {
    return 1709400000; // Mock CLOB timestamp
  }

  // --- Market Data (Live) ---
  int64_t get_price(MarketId, bool) const override {
    return 60; // Mock price
  }

  int64_t get_depth(MarketId, bool) const override {
    return 5; // Mock spread
  }

  OrderBook get_orderbook(MarketId) const override {
    return {{{59, 200}}, {{61, 250}}}; // Mock L2 book
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, 60, 62, 58, 61, 5000}}; // Mock OHLCV
  }

  // --- Gamma Market Discovery ---
  std::string gamma_get_event(const std::string &id) const override {
    return "{\"event\": \"id_" + id + "\"}";
  }

  std::string gamma_get_market(const std::string &id) const override {
    return "{\"market\": \"id_" + id + "\"}";
  }

  // --- CLOB Specifics ---
  int64_t clob_get_midpoint(MarketId market) const override { return 60; }
  int64_t clob_get_spread(MarketId market) const override { return 2; }
  int64_t clob_get_last_trade_price(MarketId market) const override {
    return 61;
  }
  double clob_get_fee_rate(MarketId market) const override { return 0.005; }
  double clob_get_tick_size(MarketId market) const override { return 0.01; }

  // --- Historical ---
  std::vector<Candlestick>
  get_historical_candlesticks(MarketId market) const override {
    return {{0, 50, 60, 48, 59, 10000}}; // Mock clob_get_prices_history
  }

  // --- Trading ---
  std::string create_order(const Order &) const override {
    return "polymarket_clob_uuid";
  }
  bool cancel_order(const std::string &) const override { return true; }

  // --- Portfolio (Mock via CLOB) ---
  int64_t get_balance() const override { return 500000; }
  std::string get_positions() const override { return "{\"positions\": []}"; }
};

static const Polymarket polymarket;

// Helper for Polymarket-specific targets
constexpr MarketTarget PolyMarket(const char *id) {
  return Market(id, polymarket);
}

} // namespace bop::markets
