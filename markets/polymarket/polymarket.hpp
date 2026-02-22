#pragma once

#include "../../core/market_base.hpp"

namespace bop::markets {

struct Polymarket : public MarketBackend {
  std::string name() const override { return "Polymarket"; }

  int64_t get_price(MarketId, bool) const override {
    return 60; // Mock price
  }

  int64_t get_depth(MarketId, bool) const override {
    return 5; // Mock spread
  }
};

static constexpr Polymarket polymarket;

} // namespace bop::markets
