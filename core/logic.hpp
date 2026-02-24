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
struct OpenOrdersTag {};

struct RiskQuery {
  enum class Type { Exposure, PnL };
  Type type;
};

template <typename Tag> struct MarketQuery {
  MarketId market;
  bool outcome_yes;
  const MarketBackend *backend = nullptr;

  inline MarketQuery<Tag> count() const { return *this; }
};

struct BalanceQuery {};

// Forward declaration of composite conditions
template <typename L, typename R> struct AndCondition;
template <typename L, typename R> struct OrCondition;

struct MarketTarget {
  MarketId market;
  const MarketBackend *backend = nullptr;

  MarketTarget resolve() const {
    if (backend && !market.resolved) {
      std::string id = backend->resolve_ticker(market.ticker);
      if (id != market.ticker) {
        return {MarketId(fnv1a(id.c_str()), id, true), backend};
      }
    }
    return *this;
  }

  inline MarketQuery<PriceTag> Price(YES_t) const {
    auto r = resolve();
    return {r.market, true, r.backend};
  }
  inline MarketQuery<PriceTag> Price(NO_t) const {
    auto r = resolve();
    return {r.market, false, r.backend};
  }
  inline MarketQuery<VolumeTag> Volume(YES_t) const {
    auto r = resolve();
    return {r.market, true, r.backend};
  }
  inline MarketQuery<VolumeTag> Volume(NO_t) const {
    auto r = resolve();
    return {r.market, false, r.backend};
  }

  // Market Depth Queries
  inline MarketQuery<DepthTag> Spread() const {
    auto r = resolve();
    return {r.market, true, r.backend};
  }
  inline MarketQuery<DepthTag> BestBid() const {
    auto r = resolve();
    return {r.market, true, r.backend};
  }
  inline MarketQuery<DepthTag> BestAsk() const {
    auto r = resolve();
    return {r.market, false, r.backend};
  }

  // WebSocket Streaming Entry Points
  inline void
  OnOrderbook(std::function<void(const OrderBook &)> callback) const {
    auto r = resolve();
    if (r.backend)
      r.backend->ws_subscribe_orderbook(r.market, callback);
  }

  inline void OnTrade(std::function<void(bop::Price, int64_t)> callback) const {
    auto r = resolve();
    if (r.backend)
      r.backend->ws_subscribe_trades(r.market, callback);
  }
};

// Spread Logic
struct SpreadTarget {
  MarketId m1;
  MarketId m2;
  const MarketBackend *backend = nullptr;

  SpreadTarget resolve() const {
    if (backend) {
        std::string id1 = m1.resolved ? m1.ticker : backend->resolve_ticker(m1.ticker);
        std::string id2 = m2.resolved ? m2.ticker : backend->resolve_ticker(m2.ticker);
        return {MarketId(fnv1a(id1.c_str()), id1, true), 
                MarketId(fnv1a(id2.c_str()), id2, true), 
                backend};
    }
    return *this;
  }
};

inline SpreadTarget operator-(MarketTarget a, MarketTarget b) {
  auto ra = a.resolve();
  auto rb = b.resolve();
  return {ra.market, rb.market, ra.backend};
}

struct MarketBoundSpread {
  int quantity;
  bool is_buy;
  SpreadTarget spread;
  int64_t timestamp_ns;
  const MarketBackend *backend = nullptr;
};

inline MarketBoundSpread operator/(const Buy &b, SpreadTarget spread) {
  auto rs = spread.resolve();
  return {b.quantity, true, rs, b.timestamp_ns, rs.backend};
}

inline MarketBoundSpread operator/(const Sell &s, SpreadTarget spread) {
  auto rs = spread.resolve();
  return {s.quantity, false, rs, s.timestamp_ns, rs.backend};
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

// Helper to identify DSL conditions
template <typename T> struct is_bop_condition : std::false_type {};

template <typename Tag, typename Q>
struct is_bop_condition<Condition<Tag, Q>> : std::true_type {};
template <typename Tag>
struct is_bop_condition<RelativeCondition<Tag>> : std::true_type {};
template <typename L, typename R>
struct is_bop_condition<AndCondition<L, R>> : std::true_type {};
template <typename L, typename R>
struct is_bop_condition<OrCondition<L, R>> : std::true_type {};

// Logical Operators for Conditions
template <typename L, typename R>
inline std::enable_if_t<is_bop_condition<L>::value ||
                            is_bop_condition<R>::value,
                        AndCondition<L, R>>
operator&&(const L &l, const R &r) {
  return {l, r};
}

template <typename L, typename R>
inline std::enable_if_t<
    is_bop_condition<L>::value || is_bop_condition<R>::value, OrCondition<L, R>>
operator||(const L &l, const R &r) {
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
  auto r = target.resolve();
  return {b.quantity, true, r.market, b.timestamp_ns, r.backend};
}

inline MarketBoundOrder operator/(const Sell &s, MarketTarget target) {
  auto r = target.resolve();
  return {s.quantity, false, r.market, s.timestamp_ns, r.backend};
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
  return MarketTarget{mkt, &b}.resolve();
}
inline MarketTarget Market(const char *name, const MarketBackend &b) {
  return MarketTarget{MarketId(name), &b}.resolve();
}

inline MarketQuery<PositionTag> Position(MarketTarget target) {
  auto r = target.resolve();
  return {r.market, true, r.backend};
}

inline MarketQuery<PositionTag> Position(MarketId mkt) { return {mkt, true}; }

inline MarketQuery<OpenOrdersTag> OpenOrders(MarketId mkt) { return {mkt, true}; }

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
