#pragma once

#include "price.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>

namespace bop {

struct MarketBackend; // Forward declaration

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
  std::string ticker; // Added for real API calls

  explicit MarketId(uint32_t h) : hash(h), ticker("") {}
  MarketId(const char *t) : hash(fnv1a(t)), ticker(t) {}
  MarketId(uint32_t h, std::string t) : hash(h), ticker(std::move(t)) {}
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

struct PegData {
  ReferencePrice ref;
  Price offset;
};

struct Order {
  MarketId market;
  int quantity;
  bool is_buy;
  bool outcome_yes;
  Price price = Price(0);
  TimeInForce tif = TimeInForce::GTC;
  bool post_only = false;
  int display_qty = 0;
  uint32_t account_hash = 0;
  Price tp_price = Price(0);
  Price sl_price = Price(0);
  SelfTradePrevention stp = SelfTradePrevention::None;
  int64_t creation_timestamp_ns = 0;
  uint64_t nonce = 0;
  const MarketBackend *backend = nullptr;

  AlgoType algo_type = AlgoType::None;
  std::variant<std::monostate, PegData, int64_t, double, Price> algo_params;

  MarketId market2 = MarketId(0u);
  bool is_spread = false;

  Order()
      : market(0u), market2(0u), is_spread(false), quantity(0), is_buy(true),
        outcome_yes(true), price(0), creation_timestamp_ns(0),
        backend(nullptr) {}

  Order(MarketId m, int q, bool b, bool y, Price p, int64_t ts)
      : market(m), market2(0u), is_spread(false), quantity(q), is_buy(b),
        outcome_yes(y), price(p), algo_type(AlgoType::None),
        algo_params(std::monostate{}), creation_timestamp_ns(ts),
        backend(nullptr) {}
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
  const MarketBackend *backend = nullptr;
};

inline MarketBoundOrder operator/(const Buy &b, MarketId market) {
  return MarketBoundOrder{b.quantity, true, market, b.timestamp_ns};
}

inline MarketBoundOrder operator/(const Buy &b, const char *market) {
  return MarketBoundOrder{b.quantity, true, MarketId(market), b.timestamp_ns};
}

inline MarketBoundOrder operator/(const Sell &s, MarketId market) {
  return MarketBoundOrder{s.quantity, false, market, s.timestamp_ns};
}

inline MarketBoundOrder operator/(const Sell &s, const char *market) {
  return MarketBoundOrder{s.quantity, false, MarketId(market), s.timestamp_ns};
}

inline Order operator/(const MarketBoundOrder &m, YES_t) {
  Order o{m.market, m.quantity, m.is_buy, true, Price(0), m.timestamp_ns};
  o.backend = m.backend;
  return o;
}

inline Order operator/(const MarketBoundOrder &m, NO_t) {
  Order o{m.market, m.quantity, m.is_buy, false, Price(0), m.timestamp_ns};
  o.backend = m.backend;
  return o;
}

} // namespace bop

// Global literals (keep out of namespace for DSL feel)
inline bop::MarketId operator""_mkt(const char *str, size_t) {
  return bop::MarketId(str);
}

constexpr bop::Account operator""_acc(const char *str, size_t) {
  return bop::Account(bop::fnv1a(str));
}

constexpr int operator"" _shares(unsigned long long int v) {
  return static_cast<int>(v);
}

constexpr bop::Price operator"" _usd(long double v) {
  return bop::Price::from_usd(static_cast<double>(v));
}

constexpr bop::Price operator"" _usd(unsigned long long int v) {
  return bop::Price::from_usd(static_cast<double>(v));
}

constexpr bop::Price operator"" _cents(unsigned long long int v) {
  return bop::Price::from_cents(static_cast<int64_t>(v));
}

constexpr int64_t operator"" _ticks(unsigned long long int v) {
  return static_cast<int64_t>(v);
}
