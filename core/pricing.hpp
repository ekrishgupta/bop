#pragma once

#include "core.hpp"

namespace bop {

// Pricing Models
struct MarketPrice {};

struct LimitPrice {
  Price price;
  explicit LimitPrice(Price p) : price(p) {}
};

struct Peg {
  ReferencePrice ref;
  Price offset;
  explicit Peg(ReferencePrice r, Price o) : ref(r), offset(o) {}
};

struct TrailingStop {
  Price trail_amount;
  explicit TrailingStop(Price t) : trail_amount(t) {}
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
  o.price = Price(0);
  return o;
}
inline Order &&operator+(Order &&o, MarketPrice) {
  o.price = Price(0);
  return std::move(o);
}

// Order + Peg -> Order
inline Order &operator+(Order &o, Peg p) {
  o.price = Price(0);
  o.algo_type = AlgoType::Peg;
  o.algo_params = PegData{p.ref, p.offset};
  return o;
}
inline Order &&operator+(Order &&o, Peg p) {
  o.price = Price(0);
  o.algo_type = AlgoType::Peg;
  o.algo_params = PegData{p.ref, p.offset};
  return std::move(o);
}

// Order + TrailingStop -> Order
inline Order &operator+(Order &o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return o;
}
inline Order &&operator+(Order &&o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return std::move(o);
}

} // namespace bop
