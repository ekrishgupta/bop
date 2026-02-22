#pragma once

#include <chrono>
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

// Compile-Time Account ID wrapper
struct Account {
  uint32_t hash;
  constexpr explicit Account(uint32_t h) : hash(h) {}
};

// User-Defined Literal for Accounts
constexpr Account operator""_acc(const char *str, size_t) {
  return Account(fnv1a(str));
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
  int64_t price;
  bool is_pegged = false;
  ReferencePrice pegged_ref = ReferencePrice::Mid;
  int64_t peg_offset = 0;
  TimeInForce tif = TimeInForce::GTC;
  bool post_only = false;
  int display_qty = 0; // 0 means not an iceberg

  // VWAP/TWAP Fields
  bool is_twap = false;
  std::chrono::seconds twap_duration{0};
  bool is_vwap = false;
  double vwap_participation = 0.0;

  // Account/Risk Routing
  uint32_t account_hash = 0; // 0 = Default Account

  // Bracket Orders (0 means not set)
  int64_t tp_price = 0;
  int64_t sl_price = 0;
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
  int64_t price;
  constexpr explicit LimitPrice(int64_t p) : price(p) {}
};

struct Peg {
  ReferencePrice ref;
  int64_t offset;
  constexpr explicit Peg(ReferencePrice r, int64_t o) : ref(r), offset(o) {}
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

struct TWAP {
  std::chrono::seconds duration;
  constexpr explicit TWAP(std::chrono::seconds d) : duration(d) {}
};

struct VWAP {
  double max_participation_rate;
  constexpr explicit VWAP(double rate) : max_participation_rate(rate) {}
};

// Bracket Order Legs
struct TakeProfit {
  int64_t price;
  constexpr explicit TakeProfit(int64_t p) : price(p) {}
};

struct StopLoss {
  int64_t price;
  constexpr explicit StopLoss(int64_t p) : price(p) {}
};

// Custom Literals for std::chrono
constexpr std::chrono::seconds operator""_sec(unsigned long long int v) {
  return std::chrono::seconds(v);
}
constexpr std::chrono::minutes operator""_min(unsigned long long int v) {
  return std::chrono::minutes(v);
}

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
constexpr Order operator|(Order o, TWAP t) {
  o.is_twap = true;
  o.twap_duration = t.duration;
  return o;
}
constexpr Order operator|(Order o, VWAP v) {
  o.is_vwap = true;
  o.vwap_participation = v.max_participation_rate;
  return o;
}

// Account Routing via operator|
constexpr Order operator|(Order o, Account a) {
  o.account_hash = a.hash;
  return o;
}

// Bracket Legs via operator&
constexpr Order operator&(Order o, TakeProfit tp) {
  o.tp_price = tp.price;
  return o;
}
constexpr Order operator&(Order o, StopLoss sl) {
  o.sl_price = sl.price;
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
  int64_t threshold;
  bool is_greater;
};

constexpr Condition operator>(MarketQuery q, int64_t t) { return {q, t, true}; }
constexpr Condition operator<(MarketQuery q, int64_t t) {
  return {q, t, false};
}

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
