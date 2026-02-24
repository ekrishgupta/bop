#pragma once

#include "core.hpp"
#include "pricing.hpp"

namespace bop {

// Time In Force Tags
struct IOC_t {};
inline static IOC_t IOC;
struct GTC_t {};
inline static GTC_t GTC;
struct FOK_t {};
inline static FOK_t FOK;

// Algo Modifiers
struct PostOnly_t {};
inline static PostOnly_t PostOnly;

// Self-Trade Prevention Tags
struct STP_t {
  SelfTradePrevention mode;
  explicit STP_t(SelfTradePrevention m) : mode(m) {}
};
inline static STP_t CancelNew{SelfTradePrevention::CancelNew};
inline static STP_t CancelOld{SelfTradePrevention::CancelOld};
inline static STP_t CancelBoth{SelfTradePrevention::CancelBoth};
inline static STP_t STP{SelfTradePrevention::CancelNew};

struct Iceberg {
  int display_qty;
  explicit Iceberg(int qty) : display_qty(qty) {
    if (qty <= 0)
      throw std::invalid_argument("Iceberg display quantity must be positive");
  }
};

struct TWAP {
  std::chrono::seconds duration;
  explicit TWAP(std::chrono::seconds d) : duration(d) {}
};

struct VWAP {
  double max_participation_rate;
  explicit VWAP(double rate) : max_participation_rate(rate) {
    if (rate <= 0.0 || rate > 1.0)
      throw std::invalid_argument("VWAP participation rate must be in (0, 1]");
  }
};

// Bracket Order Legs
struct TakeProfit {
  Price price;
  explicit TakeProfit(Price p) : price(p) {}
};

struct StopLoss {
  Price price;
  explicit StopLoss(Price p) : price(p) {}
};

// TIF Modifiers
inline Order &operator|(Order &o, IOC_t) {
  o.tif = TimeInForce::IOC;
  return o;
}
inline Order &&operator|(Order &&o, IOC_t) {
  o.tif = TimeInForce::IOC;
  return std::move(o);
}
inline Order &operator|(Order &o, GTC_t) {
  o.tif = TimeInForce::GTC;
  return o;
}
inline Order &&operator|(Order &&o, GTC_t) {
  o.tif = TimeInForce::GTC;
  return std::move(o);
}
inline Order &operator|(Order &o, FOK_t) {
  o.tif = TimeInForce::FOK;
  return o;
}
inline Order &&operator|(Order &&o, FOK_t) {
  o.tif = TimeInForce::FOK;
  return std::move(o);
}

// Algo Modifiers
inline Order &operator|(Order &o, PostOnly_t) {
  o.post_only = true;
  return o;
}
inline Order &&operator|(Order &&o, PostOnly_t) {
  o.post_only = true;
  return std::move(o);
}
inline Order &operator|(Order &o, Iceberg ib) {
  o.display_qty = ib.display_qty;
  return o;
}
inline Order &&operator|(Order &&o, Iceberg ib) {
  o.display_qty = ib.display_qty;
  return std::move(o);
}
inline Order &operator|(Order &o, TWAP t) {
  o.algo_type = AlgoType::TWAP;
  o.algo_params = static_cast<int64_t>(t.duration.count());
  return o;
}
inline Order &&operator|(Order &&o, TWAP t) {
  o.algo_type = AlgoType::TWAP;
  o.algo_params = static_cast<int64_t>(t.duration.count());
  return std::move(o);
}
inline Order &operator|(Order &o, VWAP v) {
  o.algo_type = AlgoType::VWAP;
  o.algo_params = v.max_participation_rate;
  return o;
}
inline Order &&operator|(Order &&o, VWAP v) {
  o.algo_type = AlgoType::VWAP;
  o.algo_params = v.max_participation_rate;
  return std::move(o);
}
inline Order &operator|(Order &o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return o;
}
inline Order &&operator|(Order &&o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return std::move(o);
}

// Account Routing
inline Order &operator|(Order &o, Account a) {
  o.account_hash = a.hash;
  return o;
}
inline Order &&operator|(Order &&o, Account a) {
  o.account_hash = a.hash;
  return std::move(o);
}

// STP Modifiers
inline Order &operator|(Order &o, STP_t s) {
  o.stp = s.mode;
  return o;
}
inline Order &&operator|(Order &&o, STP_t s) {
  o.stp = s.mode;
  return std::move(o);
}

// Bracket Legs
inline Order &operator&(Order &o, TakeProfit tp) {
  o.tp_price = tp.price;
  return o;
}
inline Order &&operator&(Order &&o, TakeProfit tp) {
  o.tp_price = tp.price;
  return std::move(o);
}
inline Order &operator&(Order &o, StopLoss sl) {
  o.sl_price = sl.price;
  return o;
}
inline Order &&operator&(Order &&o, StopLoss sl) {
  o.sl_price = sl.price;
  return std::move(o);
}

} // namespace bop

// Custom Literals for time
inline std::chrono::seconds operator""_sec(unsigned long long int v) {
  return std::chrono::seconds(v);
}
inline std::chrono::minutes operator""_min(unsigned long long int v) {
  return std::chrono::minutes(v);
}
