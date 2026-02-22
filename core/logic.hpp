#pragma once

#include "core.hpp"

namespace bop {

// Query Tags
struct PriceTag {};
struct VolumeTag {};
struct PositionTag {};
struct BalanceTag {};
struct SpreadTag {};
struct DepthTag {};

template <typename Tag> struct MarketQuery {
  MarketId market;
  bool outcome_yes;
};

struct BalanceQuery {};

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

  // Market Depth Queries
  constexpr MarketQuery<DepthTag> Spread() const { return {market, true}; }
  constexpr MarketQuery<DepthTag> BestBid() const { return {market, true}; }
  constexpr MarketQuery<DepthTag> BestAsk() const { return {market, false}; }
};

// Spread Logic
struct SpreadTarget {
  MarketId m1;
  MarketId m2;
};

inline SpreadTarget operator-(MarketTarget a, MarketTarget b) {
  return {a.market, b.market};
}

struct MarketBoundSpread {
  int quantity;
  bool is_buy;
  SpreadTarget spread;
  int64_t timestamp_ns;
};

inline MarketBoundSpread operator/(const Buy &b, SpreadTarget spread) {
  return {b.quantity, true, spread, b.timestamp_ns};
}

inline MarketBoundSpread operator/(const Sell &s, SpreadTarget spread) {
  return {s.quantity, false, spread, s.timestamp_ns};
}

inline Order operator/(const MarketBoundSpread &m, YES_t) {
  // Overload Order to support spread markets or handle it in engine
  Order o{m.spread.m1, m.quantity, m.is_buy, true, 0, m.timestamp_ns};
  o.algo_type = AlgoType::None; // Placeholder: Engine should detect spread
  return o;
}

template <typename Tag, typename Q = MarketQuery<Tag>> struct Condition {
  Q query;
  int64_t threshold;
  bool is_greater;
  constexpr Condition(Q q, int64_t t, bool g)
      : query(q), threshold(t), is_greater(g) {}

  // Context-aware evaluation (implemented in engine.hpp)
  bool eval() const;
  operator bool() const { return eval(); }
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
  bool eval() const { return left.eval() && right.eval(); }
  operator bool() const { return eval(); }
};

template <typename L, typename R> struct OrCondition {
  L left;
  R right;
  bool eval() const { return left.eval() || right.eval(); }
  operator bool() const { return eval(); }
};

template <typename T> struct ConditionalOrder {
  T condition;
  Order order;

  ConditionalOrder(T c, Order &&o)
      : condition(std::move(c)), order(std::move(o)) {}
  ConditionalOrder(T c, const Order &o) : condition(std::move(c)), order(o) {}
};

template <typename T> struct WhenBinder {
  T condition;
};

template <typename T> constexpr WhenBinder<T> When(T c) { return {c}; }

struct OCOOrder {
  Order order1;
  Order order2;
  bool eval() const { return true; } // OCO legs are typically always valid
};

// DSL entry for OCO
inline OCOOrder Either(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(const Order &o1, const Order &o2) {
  return {o1, o2};
}

inline OCOOrder operator||(Order &&o1, const Order &o2) {
  return {std::move(o1), o2};
}

inline OCOOrder operator||(const Order &o1, Order &&o2) {
  return {o1, std::move(o2)};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, Order &&o) {
  return {std::move(w.condition), std::move(o)};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, const Order &o) {
  return {std::move(w.condition), o};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, OCOOrder &&oco) {
  return {std::move(w.condition),
          Order(oco.order1)}; // Placeholder: simplified for demonstration
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

// Depth Comparisons
constexpr Condition<DepthTag> operator<(MarketQuery<DepthTag> q,
                                        int64_t threshold) {
  return {q, threshold, false};
}
constexpr Condition<DepthTag> operator>(MarketQuery<DepthTag> q,
                                        int64_t threshold) {
  return {q, threshold, true};
}

// Position comparisons
constexpr Condition<PositionTag> operator>(MarketQuery<PositionTag> q,
                                           int64_t shares) {
  return {q, shares, true};
}
constexpr Condition<PositionTag> operator<(MarketQuery<PositionTag> q,
                                           int64_t shares) {
  return {q, shares, false};
}

// Balance comparisons
constexpr Condition<BalanceTag, BalanceQuery> operator>(BalanceQuery q,
                                                        int64_t amount) {
  return {q, amount, true};
}
constexpr Condition<BalanceTag, BalanceQuery> operator<(BalanceQuery q,
                                                        int64_t amount) {
  return {q, amount, false};
}

} // namespace bop

// Global helper for DSL entry
constexpr bop::MarketTarget Market(bop::MarketId mkt) { return {mkt}; }
constexpr bop::MarketTarget Market(const char *name) {
  return {bop::MarketId(bop::fnv1a(name))};
}

constexpr bop::MarketQuery<bop::PositionTag> Position(bop::MarketId mkt) {
  return {mkt, true};
}

constexpr bop::BalanceQuery Balance() { return {}; }

// Batch DSL Entry
inline std::initializer_list<bop::Order>
Batch(std::initializer_list<bop::Order> list) {
  return list;
}
