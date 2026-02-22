#pragma once

#include "core.hpp"
#include "pricing.hpp"

namespace bop {

// Time In Force Tags
struct IOC_t {};
static constexpr IOC_t IOC;
struct GTC_t {};
static constexpr GTC_t GTC;
struct FOK_t {};
static constexpr FOK_t FOK;

// Algo Modifiers
struct PostOnly_t {};
static constexpr PostOnly_t PostOnly;

// Self-Trade Prevention Tags
struct STP_t {
  SelfTradePrevention mode;
  constexpr explicit STP_t(SelfTradePrevention m) : mode(m) {}
};
static constexpr STP_t CancelNew{SelfTradePrevention::CancelNew};
static constexpr STP_t CancelOld{SelfTradePrevention::CancelOld};
static constexpr STP_t CancelBoth{SelfTradePrevention::CancelBoth};
static constexpr STP_t STP{SelfTradePrevention::CancelNew};

struct Iceberg {
  int display_qty;
  constexpr explicit Iceberg(int qty) : display_qty(qty) {
    if (qty <= 0)
      throw std::invalid_argument("Iceberg display quantity must be positive");
  }
};

struct TWAP {
  std::chrono::seconds duration;
  constexpr explicit TWAP(std::chrono::seconds d) : duration(d) {}
};

struct VWAP {
  double max_participation_rate;
  constexpr explicit VWAP(double rate) : max_participation_rate(rate) {
    if (rate <= 0.0 || rate > 1.0)
      throw std::invalid_argument("VWAP participation rate must be in (0, 1]");
  }
};

// Bracket Order Legs
struct TakeProfit {
  int64_t price;
  constexpr explicit TakeProfit(int64_t p) : price(p) {
    if (p < 0)
      throw std::invalid_argument("Take profit price cannot be negative");
  }
};

struct StopLoss {
  int64_t price;
  constexpr explicit StopLoss(int64_t p) : price(p) {
    if (p < 0)
      throw std::invalid_argument("Stop loss price cannot be negative");
  }
};

// TIF Modifiers
constexpr Order &operator|(Order &o, IOC_t) {
  o.tif = TimeInForce::IOC;
  return o;
}
constexpr Order &&operator|(Order &&o, IOC_t) {
  o.tif = TimeInForce::IOC;
  return std::move(o);
}
constexpr Order &operator|(Order &o, GTC_t) {
  o.tif = TimeInForce::GTC;
  return o;
}
constexpr Order &&operator|(Order &&o, GTC_t) {
  o.tif = TimeInForce::GTC;
  return std::move(o);
}
constexpr Order &operator|(Order &o, FOK_t) {
  o.tif = TimeInForce::FOK;
  return o;
}
constexpr Order &&operator|(Order &&o, FOK_t) {
  o.tif = TimeInForce::FOK;
  return std::move(o);
}

// Algo Modifiers
constexpr Order &operator|(Order &o, PostOnly_t) {
  o.post_only = true;
  return o;
}
constexpr Order &&operator|(Order &&o, PostOnly_t) {
  o.post_only = true;
  return std::move(o);
}
constexpr Order &operator|(Order &o, Iceberg ib) {
  o.display_qty = ib.display_qty;
  return o;
}
constexpr Order &&operator|(Order &&o, Iceberg ib) {
  o.display_qty = ib.display_qty;
  return std::move(o);
}
constexpr Order &operator|(Order &o, TWAP t) {
  o.algo_type = AlgoType::TWAP;
  o.twap_duration_sec = t.duration.count();
  return o;
}
constexpr Order &&operator|(Order &&o, TWAP t) {
  o.algo_type = AlgoType::TWAP;
  o.twap_duration_sec = t.duration.count();
  return std::move(o);
}
constexpr Order &operator|(Order &o, VWAP v) {
  o.algo_type = AlgoType::VWAP;
  o.vwap_participation = v.max_participation_rate;
  return o;
}
constexpr Order &&operator|(Order &&o, VWAP v) {
  o.algo_type = AlgoType::VWAP;
  o.vwap_participation = v.max_participation_rate;
  return std::move(o);
}
constexpr Order &operator|(Order &o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.trail_amount = ts.trail_amount;
  return o;
}
constexpr Order &&operator|(Order &&o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.trail_amount = ts.trail_amount;
  return std::move(o);
}

// Account Routing
constexpr Order &operator|(Order &o, Account a) {
  o.account_hash = a.hash;
  return o;
}
constexpr Order &&operator|(Order &&o, Account a) {
  o.account_hash = a.hash;
  return std::move(o);
}

// STP Modifiers
constexpr Order &operator|(Order &o, STP_t s) {
  o.stp = s.mode;
  return o;
}
constexpr Order &&operator|(Order &&o, STP_t s) {
  o.stp = s.mode;
  return std::move(o);
}

// Bracket Legs
constexpr Order &operator&(Order &o, TakeProfit tp) {
  o.tp_price = tp.price;
  return o;
}
constexpr Order &&operator&(Order &&o, TakeProfit tp) {
  o.tp_price = tp.price;
  return std::move(o);
}
constexpr Order &operator&(Order &o, StopLoss sl) {
  o.sl_price = sl.price;
  return o;
}
constexpr Order &&operator&(Order &&o, StopLoss sl) {
  o.sl_price = sl.price;
  return std::move(o);
}

} // namespace bop

// Custom Literals for time
constexpr std::chrono::seconds operator""_sec(unsigned long long int v) {
  return std::chrono::seconds(v);
}
constexpr std::chrono::minutes operator""_min(unsigned long long int v) {
  return std::chrono::minutes(v);
}
