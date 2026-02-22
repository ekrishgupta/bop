#pragma once

#include "core.hpp"

namespace bop {

// Query Tags
struct PriceTag {};
struct VolumeTag {};

template <typename Tag> struct MarketQuery {
  MarketId market;
  bool outcome_yes;
};

struct MarketTarget {
  MarketId market;
  constexpr MarketQuery<PriceTag> Price(YES_t) const { return {market, true}; }
  constexpr MarketQuery<PriceTag> Price(NO_t) const { return {market, false}; }
  constexpr MarketQuery<VolumeTag> Volume(YES_t) const {
    return {market, true};
  }
  constexpr MarketQuery<VolumeTag> Volume(NO_t) const {
    return {market, false};
  }
};

template <typename Tag> struct Condition {
  MarketQuery<Tag> query;
  int64_t threshold;
  bool is_greater;
  constexpr Condition(MarketQuery<Tag> q, int64_t t, bool g)
      : query(q), threshold(t), is_greater(g) {}
};

// Price Comparisons
constexpr Condition<PriceTag> operator>(MarketQuery<PriceTag> q,
                                        int64_t ticks) {
  return {q, ticks, true};
}
constexpr Condition<PriceTag> operator<(MarketQuery<PriceTag> q,
                                        int64_t ticks) {
  return {q, ticks, false};
}

// Delete logical errors to prevent rounding issues from floating point
Condition<PriceTag> operator>(MarketQuery<PriceTag> q, double t) = delete;
Condition<PriceTag> operator<(MarketQuery<PriceTag> q, double t) = delete;

// Volume Comparisons
constexpr Condition<VolumeTag> operator>(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), true};
}
constexpr Condition<VolumeTag> operator<(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), false};
}

// Delete logical errors
Condition<VolumeTag> operator>(MarketQuery<VolumeTag> q, double t) = delete;
Condition<VolumeTag> operator<(MarketQuery<VolumeTag> q, double t) = delete;

template <typename Tag> struct ConditionalOrder {
  Condition<Tag> condition;
  Order order;
};

template <typename Tag> struct WhenBinder {
  Condition<Tag> condition;
};

template <typename Tag> constexpr WhenBinder<Tag> When(Condition<Tag> c) {
  return {c};
}

template <typename Tag>
constexpr ConditionalOrder<Tag> operator>>(WhenBinder<Tag> w, Order o) {
  return {w.condition, o};
}

struct OCOOrder {
  Order order1;
  Order order2;
};

constexpr OCOOrder operator||(const Order &o1, const Order &o2) {
  return {o1, o2};
}

} // namespace bop

// Global helper for DSL entry
constexpr bop::MarketTarget Market(bop::MarketId mkt) { return {mkt}; }
