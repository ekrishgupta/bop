#pragma once

#include "core.hpp"

namespace bop {

// Pricing Models
struct MarketPrice {};

struct LimitPrice {
  int64_t price;
  constexpr explicit LimitPrice(int64_t p) : price(p) {
    if (p < 0)
      throw std::invalid_argument("Limit price cannot be negative");
  }
};

struct Peg {
  ReferencePrice ref;
  int64_t offset;
  constexpr explicit Peg(ReferencePrice r, int64_t o) : ref(r), offset(o) {}
};

struct TrailingStop {
  int64_t trail_amount;
  constexpr explicit TrailingStop(int64_t t) : trail_amount(t) {
    if (t < 0)
      throw std::invalid_argument("Trailing stop amount cannot be negative");
  }
};

// OutcomeBoundOrder + LimitPrice -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, LimitPrice lp) {
  return Order{o.market, o.quantity, o.is_buy, o.outcome_yes, lp.price};
}

// OutcomeBoundOrder + MarketPrice -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, MarketPrice) {
  return Order{o.market, o.quantity, o.is_buy, o.outcome_yes, 0};
}

// OutcomeBoundOrder + Peg -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, Peg p) {
  Order ord{o.market, o.quantity, o.is_buy, o.outcome_yes, 0};
  ord.algo_type = AlgoType::Peg;
  ord.peg = {p.ref, p.offset};
  return ord;
}

} // namespace bop
