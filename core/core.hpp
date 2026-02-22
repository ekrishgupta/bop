#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace bop {

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

// Compile-Time Account ID wrapper
struct Account {
  uint32_t hash;
  constexpr explicit Account(uint32_t h) : hash(h) {}
};

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
  int display_qty = 0;

  bool is_twap = false;
  std::chrono::seconds twap_duration{0};
  bool is_vwap = false;
  double vwap_participation = 0.0;

  uint32_t account_hash = 0;
  int64_t tp_price = 0;
  int64_t sl_price = 0;
  bool is_trailing_stop = false;
  int64_t trail_amount = 0;
};

// Action Types
struct Buy {
  int quantity;
  constexpr explicit Buy(int q) : quantity(q) {
    if (q <= 0)
      throw std::invalid_argument("Buy quantity must be positive");
  }
};

struct Sell {
  int quantity;
  constexpr explicit Sell(int q) : quantity(q) {
    if (q <= 0)
      throw std::invalid_argument("Sell quantity must be positive");
  }
};

// Intermediate DSL structure: Market Bound
struct MarketBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
};

// Intermediate DSL structure: Outcome Bound
struct OutcomeBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
  bool outcome_yes;
};

constexpr MarketBoundOrder operator/(const Buy &b, MarketId market) {
  return MarketBoundOrder{b.quantity, true, market};
}

constexpr MarketBoundOrder operator/(const Buy &b, const char *market) {
  return MarketBoundOrder{b.quantity, true, MarketId(fnv1a(market))};
}

constexpr MarketBoundOrder operator/(const Sell &s, MarketId market) {
  return MarketBoundOrder{s.quantity, false, market};
}

constexpr MarketBoundOrder operator/(const Sell &s, const char *market) {
  return MarketBoundOrder{s.quantity, false, MarketId(fnv1a(market))};
}

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, YES_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, true};
}

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, NO_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, false};
}

} // namespace bop

// Global literals (keep out of namespace for DSL feel)
constexpr bop::MarketId operator""_mkt(const char *str, size_t) {
  return bop::MarketId(bop::fnv1a(str));
}

constexpr bop::Account operator""_acc(const char *str, size_t) {
  return bop::Account(bop::fnv1a(str));
}

constexpr int operator"" _shares(unsigned long long int v) {
  return static_cast<int>(v);
}

constexpr int64_t operator"" _ticks(unsigned long long int v) {
  return static_cast<int64_t>(v);
}
