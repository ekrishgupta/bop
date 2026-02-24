#pragma once

#include "core.hpp"
#include "market_base.hpp"
#include <functional>

namespace bop {

// Query Tags
struct PriceTag {};
struct VolumeTag {};
struct PositionTag {};
struct BalanceTag {};
struct ExposureTag {};
struct PnLTag {};
struct SpreadTag {};
struct DepthTag {};

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

  inline MarketQuery<PriceTag> Price(YES_t) const {
    return {market, true, backend};
  }
  inline MarketQuery<PriceTag> Price(NO_t) const {
    return {market, false, backend};
  }
  inline MarketQuery<VolumeTag> Volume(YES_t) const {
    return {market, true, backend};
  }
  inline MarketQuery<VolumeTag> Volume(NO_t) const {
    return {market, false, backend};
  }

  // Market Depth Queries
  inline MarketQuery<DepthTag> Spread() const {
    return {market, true, backend};
  }
  inline MarketQuery<DepthTag> BestBid() const {
    return {market, true, backend};
  }
  inline MarketQuery<DepthTag> BestAsk() const {
    return {market, false, backend};
  }

  // WebSocket Streaming Entry Points
  inline void
  OnOrderbook(std::function<void(const OrderBook &)> callback) const {
    if (backend)
      backend->ws_subscribe_orderbook(market, callback);
  }

  inline void OnTrade(std::function<void(bop::Price, int64_t)> callback) const {
    if (backend)
      backend->ws_subscribe_trades(market, callback);
  }
};

// Spread Logic
struct SpreadTarget {
  MarketId m1;
  MarketId m2;
  const MarketBackend *backend = nullptr;
};

inline SpreadTarget operator-(MarketTarget a, MarketTarget b) {
  return {a.market, b.market, a.backend};
}

struct MarketBoundSpread {
  int quantity;
  bool is_buy;
  SpreadTarget spread;
  int64_t timestamp_ns;
  const MarketBackend *backend = nullptr;
};

inline MarketBoundSpread operator/(const Buy &b, SpreadTarget spread) {
  return {b.quantity, true, spread, b.timestamp_ns, spread.backend};
}

inline MarketBoundSpread operator/(const Sell &s, SpreadTarget spread) {
  return {s.quantity, false, spread, s.timestamp_ns, spread.backend};
}

inline Order operator/(const MarketBoundSpread &m, YES_t) {
  Order o{m.spread.m1, m.quantity, m.is_buy, true, Price(0), m.timestamp_ns};
  o.market2 = m.spread.m2;
  o.is_spread = true;
  o.backend = m.backend;
  return o;
}

inline Order operator/(const MarketBoundSpread &m, NO_t) {
  Order o{m.spread.m1, m.quantity, m.is_buy, false, Price(0), m.timestamp_ns};
  o.market2 = m.spread.m2;
  o.is_spread = true;
  o.backend = m.backend;
  return o;
}

template <typename Tag, typename Q = MarketQuery<Tag>> struct Condition {
  Q query;
  int64_t threshold;
  bool is_greater;
  Condition(Q q, int64_t t, bool g) : query(q), threshold(t), is_greater(g) {}

  bool eval() const;
};

template <typename Tag> struct RelativeCondition {
  MarketQuery<Tag> left;
  MarketQuery<Tag> right;
  bool is_greater;

  bool eval() const;
};

// Logical Operators for Conditions
template <typename L, typename R>
inline AndCondition<L, R> operator&&(const L &l, const R &r) {
  return {l, r};
}

template <typename L, typename R>
inline OrCondition<L, R> operator||(const L &l, const R &r) {
  return {l, r};
}

// Composition structures
template <typename L, typename R> struct AndCondition {
  L left;
  R right;
  inline bool eval() const { return left.eval() && right.eval(); }
};

template <typename L, typename R> struct OrCondition {
  L left;
  R right;
  inline bool eval() const { return left.eval() || right.eval(); }
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

template <typename T> inline WhenBinder<T> When(T c) { return {c}; }

struct OCOOrder {
  Order order1;
  Order order2;
  inline bool eval() const { return true; }
};

inline OCOOrder Either(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(Order &&o1, Order &&o2) {
  return {std::move(o1), std::move(o2)};
}

inline OCOOrder operator||(const Order &o1, const Order &o2) {
  return {o1, o2};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, Order &&o) {
  return {std::move(w.condition), std::move(o)};
}

template <typename T>
inline ConditionalOrder<T> operator>>(WhenBinder<T> w, const Order &o) {
  return {std::move(w.condition), o};
}

inline MarketBoundOrder operator/(const Buy &b, MarketTarget target) {
  return {b.quantity, true, target.market, b.timestamp_ns, target.backend};
}

inline MarketBoundOrder operator/(const Sell &s, MarketTarget target) {
  return {s.quantity, false, target.market, s.timestamp_ns, target.backend};
}

// Relative Comparisons
template <typename Tag>
inline RelativeCondition<Tag> operator<(MarketQuery<Tag> a,
                                        MarketQuery<Tag> b) {
  return {a, b, false};
}

template <typename Tag>
inline RelativeCondition<Tag> operator>(MarketQuery<Tag> a,
                                        MarketQuery<Tag> b) {
  return {a, b, true};
}

// Price Comparisons
inline Condition<PriceTag> operator>(MarketQuery<PriceTag> q, Price threshold) {
  return {q, threshold.raw, true};
}
inline Condition<PriceTag> operator<(MarketQuery<PriceTag> q, Price threshold) {
  return {q, threshold.raw, false};
}

inline Condition<PriceTag> operator>(MarketQuery<PriceTag> q, double price) {
  return {q, Price::from_double(price).raw, true};
}
inline Condition<PriceTag> operator<(MarketQuery<PriceTag> q, double price) {
  return {q, Price::from_double(price).raw, false};
}

// Volume Comparisons
inline Condition<VolumeTag> operator>(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), true};
}
inline Condition<VolumeTag> operator<(MarketQuery<VolumeTag> q, int t) {
  return {q, static_cast<int64_t>(t), false};
}

// Depth Comparisons
inline Condition<DepthTag> operator<(MarketQuery<DepthTag> q,
                                     int64_t threshold) {
  return {q, threshold, false};
}
inline Condition<DepthTag> operator>(MarketQuery<DepthTag> q,
                                     int64_t threshold) {
  return {q, threshold, true};
}

// Position comparisons
inline Condition<PositionTag> operator>(MarketQuery<PositionTag> q,
                                        int64_t shares) {
  return {q, shares, true};
}
inline Condition<PositionTag> operator<(MarketQuery<PositionTag> q,
                                        int64_t shares) {
  return {q, shares, false};
}

// Balance comparisons
inline Condition<BalanceTag, BalanceQuery> operator>(BalanceQuery q,
                                                     int64_t amount) {
  return {q, amount, true};
}
inline Condition<BalanceTag, BalanceQuery> operator<(BalanceQuery q,
                                                     int64_t amount) {
  return {q, amount, false};
}

// Global helper for DSL entry
inline MarketTarget Market(MarketId mkt) { return {mkt, nullptr}; }
inline MarketTarget Market(const char *name) {
  return {MarketId(name), nullptr};
}

inline MarketTarget Market(MarketId mkt, const MarketBackend &b) {
  return {mkt, &b};
}
inline MarketTarget Market(const char *name, const MarketBackend &b) {
  return {MarketId(name), &b};
}

inline MarketQuery<PositionTag> Position(MarketId mkt) { return {mkt, true}; }

inline BalanceQuery Balance() { return {}; }

inline Condition<ExposureTag, RiskQuery> Exposure() {
  return {{RiskQuery::Type::Exposure}, 0, false};
}

inline Condition<PnLTag, RiskQuery> PnL() {
  return {{RiskQuery::Type::PnL}, 0, false};
}

// Exposure/PnL comparisons
inline Condition<ExposureTag, RiskQuery>
operator<(Condition<ExposureTag, RiskQuery> c, long long threshold) {
  c.threshold = threshold;
  c.is_greater = false;
  return c;
}

// Batch DSL Entry
inline std::initializer_list<Order> Batch(std::initializer_list<Order> list) {
  return list;
}

} // namespace bop
