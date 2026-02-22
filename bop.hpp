#pragma once

#include <cstddef>
#include <cstdint>

// FNV-1a Constants
constexpr uint32_t FNV_PRIME = 16777619u;
constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;

// Constexpr Compile-Time String Hashing (FNV-1a)
constexpr uint32_t fnv1a(const char *str, uint32_t hash = FNV_OFFSET_BASIS) {
  return *str == '\0'
             ? hash
             : fnv1a(str + 1, (hash ^ static_cast<uint32_t>(*str)) * FNV_PRIME);
}

// Compile-Time Market ID wrapper
struct MarketId {
  uint32_t hash;
  constexpr explicit MarketId(uint32_t h) : hash(h) {}
};

// User-Defined Literal for Market IDs
constexpr MarketId operator""_mkt(const char *str, size_t) {
  return MarketId(fnv1a(str));
}

// Outcome tags
struct YES_t {};
static constexpr YES_t YES;
struct NO_t {};
static constexpr NO_t NO;

// Time In Force
enum class TimeInForce { GTC, IOC, FOK };

// Pegged Pricing Models
enum class ReferencePrice { Bid, Ask, Mid };
constexpr ReferencePrice Bid = ReferencePrice::Bid;
constexpr ReferencePrice Ask = ReferencePrice::Ask;
constexpr ReferencePrice Mid = ReferencePrice::Mid;

// The Order "State Machine"
struct Order {
  MarketId market;
  int quantity;
  bool is_buy;
  bool outcome_yes;
  double price;
  bool is_pegged = false;
  ReferencePrice pegged_ref = ReferencePrice::Mid;
  double peg_offset = 0.0;
  TimeInForce tif = TimeInForce::GTC;
  bool post_only = false;
  int display_qty = 0; // 0 means not an iceberg
};

// Action Types
struct Buy {
  int quantity;
  constexpr explicit Buy(int q) : quantity(q) {}
};

struct Sell {
  int quantity;
  constexpr explicit Sell(int q) : quantity(q) {}
};

// User-Defined Literals for quantities
constexpr int operator"" _shares(unsigned long long int v) {
  return static_cast<int>(v);
}

// Intermediate DSL structure: Market Bound
struct MarketBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
};

constexpr MarketBoundOrder operator/(const Buy &b, MarketId market) {
  return MarketBoundOrder{b.quantity, true, market};
}

constexpr MarketBoundOrder operator/(const Sell &s, MarketId market) {
  return MarketBoundOrder{s.quantity, false, market};
}

// Pricing Models
struct MarketPrice {};

struct LimitPrice {
  double price;
  constexpr explicit LimitPrice(double p) : price(p) {}
};

struct Peg {
  ReferencePrice ref;
  double offset;
  constexpr explicit Peg(ReferencePrice r, double o) : ref(r), offset(o) {}
};

// Time In Force (TIF) Tags
struct IOC_t {};
static constexpr IOC_t IOC; // Immediate Or Cancel
struct GTC_t {};
static constexpr GTC_t GTC; // Good Til Cancelled
struct FOK_t {};
static constexpr FOK_t FOK; // Fill Or Kill

// Algo Modifiers
struct PostOnly_t {};
static constexpr PostOnly_t PostOnly;
struct Iceberg {
  int display_qty;
  constexpr explicit Iceberg(int qty) : display_qty(qty) {}
};

// Intermediate DSL structure: Outcome Bound
struct OutcomeBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
  bool outcome_yes;
};

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, YES_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, true};
}

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, NO_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, false};
}

// OutcomeBoundOrder + LimitPrice -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, LimitPrice lp) {
  return Order{
      o.market, o.quantity,          o.is_buy, o.outcome_yes,    lp.price,
      false,    ReferencePrice::Mid, 0.0,      TimeInForce::GTC, false,
      0};
}

// OutcomeBoundOrder + MarketPrice -> Order (Price = 0.0 or Inf conceptually, 0
// for now)
constexpr Order operator+(const OutcomeBoundOrder &o, MarketPrice) {
  return Order{o.market, o.quantity,          o.is_buy, o.outcome_yes,    0.0,
               false,    ReferencePrice::Mid, 0.0,      TimeInForce::GTC, false,
               0};
}

// OutcomeBoundOrder + Peg -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, Peg p) {
  return Order{o.market, o.quantity, o.is_buy,         o.outcome_yes, 0.0, true,
               p.ref,    p.offset,   TimeInForce::GTC, false,         0};
}

// TIF Modifiers via operator|
constexpr Order operator|(Order o, IOC_t) {
  o.tif = TimeInForce::IOC;
  return o;
}
constexpr Order operator|(Order o, GTC_t) {
  o.tif = TimeInForce::GTC;
  return o;
}
constexpr Order operator|(Order o, FOK_t) {
  o.tif = TimeInForce::FOK;
  return o;
}

// Algo Modifiers via operator|
constexpr Order operator|(Order o, PostOnly_t) {
  o.post_only = true;
  return o;
}
constexpr Order operator|(Order o, Iceberg ib) {
  o.display_qty = ib.display_qty;
  return o;
}

// Conditional Triggers
struct MarketQuery {
  MarketId market;
  bool outcome_yes;
};

struct MarketTarget {
  MarketId market;
  constexpr MarketQuery Price(YES_t) const { return {market, true}; }
  constexpr MarketQuery Price(NO_t) const { return {market, false}; }
};
constexpr MarketTarget Market(MarketId mkt) { return {mkt}; }

struct Condition {
  MarketQuery query;
  double threshold;
  bool is_greater;
};

constexpr Condition operator>(MarketQuery q, double t) { return {q, t, true}; }
constexpr Condition operator<(MarketQuery q, double t) { return {q, t, false}; }

struct ConditionalOrder {
  Condition condition;
  Order order;
};

struct WhenBinder {
  Condition condition;
};

constexpr WhenBinder When(Condition c) { return {c}; }

constexpr ConditionalOrder operator>>(WhenBinder w, Order o) {
  return {w.condition, o};
}

// Execution Engine Mock for Dispatching
struct ExecutionEngine {
  // In a real system, might contain connection state or ring buffer index.
};

// Global instance for testing the syntax
extern ExecutionEngine LiveExchange;

// Final Dispatch: Order >> ExecutionEngine
inline void operator>>(const Order &o, ExecutionEngine &) {
  // In a real HFT system, this would write the order into a memory-mapped
  // ring buffer or directly construct a packet for the exchange via NIC
  // bypassing.
  (void)o; // Prevent unused warning in mock
}

// Final Dispatch: ConditionalOrder >> ExecutionEngine
inline void operator>>(const ConditionalOrder &co, ExecutionEngine &) {
  (void)co; // Register conditional trigger in real system
}
