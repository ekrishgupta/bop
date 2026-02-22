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

// Order + LimitPrice -> Order
inline Order &operator+(Order &o, LimitPrice lp) {
  o.price = lp.price;
  return o;
}
inline Order &&operator+(Order &&o, LimitPrice lp) {
  o.price = lp.price;
  return std::move(o);
}

// Order + MarketPrice -> Order
inline Order &operator+(Order &o, MarketPrice) {
  o.price = 0;
  return o;
}
inline Order &&operator+(Order &&o, MarketPrice) {
  o.price = 0;
  return std::move(o);
}

// Order + Peg -> Order
inline Order &operator+(Order &o, Peg p) {
  o.price = 0;
  o.algo_type = AlgoType::Peg;
  o.peg = {p.ref, p.offset};
  return o;
}
inline Order &&operator+(Order &&o, Peg p) {
  o.price = 0;
  o.algo_type = AlgoType::Peg;
  o.peg = {p.ref, p.offset};
  return std::move(o);
}

} // namespace bop
