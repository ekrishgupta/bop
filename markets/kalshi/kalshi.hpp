#pragma once

#include "../../core/market_base.hpp"

namespace bop::markets {

struct Kalshi : public MarketBackend {
  std::string name() const override { return "Kalshi"; }

  // Historical Cutoffs (RFC3339 formatted as per docs)
  struct Cutoffs {
    std::string market_settled_ts;
    std::string trades_created_ts;
    std::string orders_updated_ts;
  } cutoffs;

  // --- Exchange & Status ---
  std::string get_exchange_status() const override { return "active"; }
  std::string get_exchange_schedule() const override { return "24/7"; }

  // --- Market Data ---
  int64_t get_price(MarketId, bool) const override {
    return 50; // Mock: prices are 1-99 cents in Kalshi
  }

  int64_t get_depth(MarketId, bool) const override {
    return 2; // Mock: spread in cents
  }

  OrderBook get_orderbook(MarketId) const override {
    return {{{50, 100}}, {{51, 100}}}; // Mock L2
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, 50, 55, 45, 52, 1000}}; // Mock OHLCV
  }

  // --- Historical ---
  std::string get_historical_cutoff() const override {
    return "2023-11-07T05:31:56Z";
  }

  // --- Trading ---
  std::string create_order(const Order &) const override {
    return "kalshi_ord_123";
  }
  bool cancel_order(const std::string &) const override { return true; }

  // --- Portfolio ---
  int64_t get_balance() const override { return 100000; }
  PortfolioSummary get_portfolio_summary() const override {
    return {100000, 5000, 2000, 105000};
  }

  // Cent-pricing validator
  static constexpr bool is_valid_price(int64_t cents) {
    return cents >= 1 && cents <= 99;
  }
};

static const Kalshi kalshi;

// Helper for Kalshi-specific tickers
constexpr MarketTarget KalshiTicker(const char *ticker) {
  return Market(ticker, kalshi);
}

} // namespace bop::markets
