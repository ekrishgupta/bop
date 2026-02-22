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

  int64_t get_price(MarketId, bool) const override {
    return 50; // Mock: prices are 1-99 cents in Kalshi
  }

  int64_t get_depth(MarketId, bool) const override {
    return 2; // Mock: spread in cents
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
