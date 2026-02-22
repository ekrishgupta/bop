#pragma once

#include "core.hpp"
#include "market_base.hpp"

namespace bop {

// Query Tags
struct PriceTag {};
struct VolumeTag {};
struct PositionTag {};
struct BalanceTag {};
struct SpreadTag {};
struct DepthTag {};
struct ExposureTag {};
struct PnLTag {};

struct RiskQuery {
  enum class Type { Exposure, PnL };
  Type type;
};

template <typename Tag> struct MarketQuery {
  MarketId market;
  bool outcome_yes;
  const MarketBackend *backend = nullptr;
};

struct BalanceQuery {};

// Forward declaration of composite conditions
template <typename L, typename R> struct AndCondition;
template <typename L, typename R> struct OrCondition;

struct MarketTarget {
  MarketId market;
  const MarketBackend *backend = nullptr;

  constexpr MarketQuery<PriceTag> Price(YES_t) const {
    return {market, true, backend};
  }
  constexpr MarketQuery<PriceTag> Price(NO_t) const {
    return {market, false, backend};
  }
  constexpr MarketQuery<VolumeTag> Volume(YES_t) const {
    return {market, true, backend};
  }
  constexpr MarketQuery<VolumeTag> Volume(NO_t) const {
    return {market, false, backend};
  }

  // Market Depth Queries
  constexpr MarketQuery<DepthTag> Spread() const {
    return {market, true, backend};
  }
  constexpr MarketQuery<DepthTag> BestBid() const {
    return {market, true, backend};
  }
  constexpr MarketQuery<DepthTag> BestAsk() const {
    return {market, false, backend};
  }
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
  // Removed operator bool() to avoid ambiguity in comparisons like Exposure() <
  // 100
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
};

template <typename L, typename R> struct OrCondition {
  L left;
  R right;
  bool eval() const { return left.eval() || right.eval(); }
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
constexpr bop::MarketTarget Market(bop::MarketId mkt) { return {mkt, nullptr}; }
constexpr bop::MarketTarget Market(const char *name) {
  return {bop::MarketId(bop::fnv1a(name)), nullptr};
}

constexpr bop::MarketTarget Market(bop::MarketId mkt,
                                   const bop::MarketBackend &b) {
  return {mkt, &b};
}
constexpr bop::MarketTarget Market(const char *name,
                                   const bop::MarketBackend &b) {
  return {bop::MarketId(bop::fnv1a(name)), &b};
}

constexpr bop::MarketQuery<bop::PositionTag> Position(bop::MarketId mkt) {
  return {mkt, true};
}

constexpr bop::BalanceQuery Balance() { return {}; }

constexpr bop::Condition<bop::ExposureTag, bop::RiskQuery> Exposure() {
  return {{bop::RiskQuery::Type::Exposure}, 0, false};
}

constexpr bop::Condition<bop::PnLTag, bop::RiskQuery> PnL() {
  return {{bop::RiskQuery::Type::PnL}, 0, false};
}

// Exposure/PnL comparisons
inline bop::Condition<bop::ExposureTag, bop::RiskQuery>
operator<(bop::Condition<bop::ExposureTag, bop::RiskQuery> c,
          long long threshold) {
  c.threshold = threshold;
  c.is_greater = false;
  return c;
}

// Batch DSL Entry
inline std::initializer_list<bop::Order>
Batch(std::initializer_list<bop::Order> list) {
  return list;
}
