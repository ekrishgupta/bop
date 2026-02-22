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

// Forward declaration of composite conditions
template <typename L, typename R> struct AndCondition;
template <typename L, typename R> struct OrCondition;

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

// Logical Operators for Conditions
template <typename L, typename R>
constexpr AndCondition<L, R> operator&&(const L &l, const R &r) {
  return {l, r};
}

template <typename L, typename R>
constexpr OrCondition<L, R> operator||(const L &l, const R &r) {
  return {l, r};
}

// Composition structures
template <typename L, typename R> struct AndCondition {
  L left;
  R right;
};

template <typename L, typename R> struct OrCondition {
  L left;
  R right;
};

template <typename T> struct ConditionalOrder {
  T condition;
  Order order;
};

template <typename T> struct WhenBinder {
  T condition;
};

template <typename T> constexpr WhenBinder<T> When(T c) { return {c}; }

template <typename T>
constexpr ConditionalOrder<T> operator>>(WhenBinder<T> w, Order o) {
  return {w.condition, o};
}

template <typename T>
constexpr ConditionalOrder<T> operator>>(WhenBinder<T> w,
                                         OutcomeBoundOrder oo) {
  return {w.condition, {oo.market, oo.quantity, oo.is_buy, oo.outcome_yes, 0}};
}

// Price Comparisons (restored with double support for DSL ease)
constexpr Condition<PriceTag> operator>(MarketQuery<PriceTag> q,
                                        int64_t ticks) {
  return {q, ticks, true};
}
constexpr Condition<PriceTag> operator<(MarketQuery<PriceTag> q,
                                        int64_t ticks) {
  return {q, ticks, false};
}

constexpr Condition<PriceTag> operator>(MarketQuery<PriceTag> q, double price) {
  return {q, static_cast<int64_t>(price * 100), true};
}
constexpr Condition<PriceTag> operator<(MarketQuery<PriceTag> q, double price) {
  return {q, static_cast<int64_t>(price * 100), false};
}

// Volume Comparisons
constexpr Condition<VolumeTag> operator>(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), true};
}
constexpr Condition<VolumeTag> operator<(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), false};
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
constexpr bop::MarketTarget Market(const char *name) {
  return {bop::MarketId(bop::fnv1a(name))};
}
