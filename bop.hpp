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

// The Order "State Machine"
struct Order {
  MarketId market;
  int quantity;
  bool is_buy;
  bool outcome_yes;
  double price;
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
  return Order{o.market, o.quantity, o.is_buy, o.outcome_yes, lp.price};
}

// OutcomeBoundOrder + MarketPrice -> Order (Price = 0.0 or Inf conceptually, 0
// for now)
constexpr Order operator+(const OutcomeBoundOrder &o, MarketPrice) {
  return Order{o.market, o.quantity, o.is_buy, o.outcome_yes, 0.0};
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
