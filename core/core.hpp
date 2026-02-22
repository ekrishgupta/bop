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

// Self-Trade Prevention Modes
enum class SelfTradePrevention { None, CancelNew, CancelOld, CancelBoth };

// Pegged Pricing Models
enum class ReferencePrice { Bid, Ask, Mid };
constexpr ReferencePrice Bid = ReferencePrice::Bid;
constexpr ReferencePrice Ask = ReferencePrice::Ask;
constexpr ReferencePrice Mid = ReferencePrice::Mid;

enum class AlgoType : uint8_t { None, Peg, TWAP, VWAP, Trailing };

// The Order "State Machine"
struct Order {
  MarketId market;
  int quantity;
  bool is_buy;
  bool outcome_yes;
  int64_t price = 0;
  TimeInForce tif = TimeInForce::GTC;
  bool post_only = false;
  int display_qty = 0;
  uint32_t account_hash = 0;
  int64_t tp_price = 0;
  int64_t sl_price = 0;
  SelfTradePrevention stp = SelfTradePrevention::None;
  int64_t creation_timestamp_ns = 0;

  AlgoType algo_type = AlgoType::None;
  union {
    struct {
      ReferencePrice ref;
      int64_t offset;
    } peg;
    int64_t twap_duration_sec;
    double vwap_participation;
    int64_t trail_amount;
  };

  Order(MarketId m, int q, bool b, bool y, int64_t p, int64_t ts)
      : market(m), quantity(q), is_buy(b), outcome_yes(y), price(p),
        algo_type(AlgoType::None), peg({ReferencePrice::Mid, 0}),
        creation_timestamp_ns(ts) {}
};

// Action Types
struct Buy {
  int quantity;
  int64_t timestamp_ns;
  explicit Buy(int q) : quantity(q) {
    if (q <= 0)
      throw std::invalid_argument("Buy quantity must be positive");
    timestamp_ns =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
  }
};

struct Sell {
  int quantity;
  int64_t timestamp_ns;
  explicit Sell(int q) : quantity(q) {
    if (q <= 0)
      throw std::invalid_argument("Sell quantity must be positive");
    timestamp_ns =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
  }
};

// Intermediate DSL structure: Market Bound
struct MarketBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
  int64_t timestamp_ns;
};

// Intermediate DSL structure: Outcome Bound
struct OutcomeBoundOrder {
  int quantity;
  bool is_buy;
  MarketId market;
  bool outcome_yes;
  int64_t timestamp_ns;
};

inline MarketBoundOrder operator/(const Buy &b, MarketId market) {
  return MarketBoundOrder{b.quantity, true, market, b.timestamp_ns};
}

inline MarketBoundOrder operator/(const Buy &b, const char *market) {
  return MarketBoundOrder{b.quantity, true, MarketId(fnv1a(market)),
                          b.timestamp_ns};
}

inline MarketBoundOrder operator/(const Sell &s, MarketId market) {
  return MarketBoundOrder{s.quantity, false, market, s.timestamp_ns};
}

inline MarketBoundOrder operator/(const Sell &s, const char *market) {
  return MarketBoundOrder{s.quantity, false, MarketId(fnv1a(market)),
                          s.timestamp_ns};
}

inline OutcomeBoundOrder operator/(const MarketBoundOrder &m, YES_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, true,
                           m.timestamp_ns};
}

inline OutcomeBoundOrder operator/(const MarketBoundOrder &m, NO_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, false,
                           m.timestamp_ns};
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
