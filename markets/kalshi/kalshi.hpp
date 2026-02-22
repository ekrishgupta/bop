#pragma once

#include "../../core/market_base.hpp"

namespace bop::markets {

struct Kalshi : public MarketBackend {
  std::string name() const override { return "Kalshi"; }

  int64_t get_price(MarketId, bool) const override {
    return 50; // Mock price
  }

  int64_t get_depth(MarketId, bool) const override {
    return 2; // Mock spread
  }
};

static constexpr Kalshi kalshi;

} // namespace bop::markets
