#pragma once

#include "core.hpp"
#include "market_base.hpp"
#include <chrono>
#include <memory>

namespace bop {

struct ExecutionEngine;

template <typename Derived> class AlgoCRTP {
public:
  bool tick(ExecutionEngine &engine) {
    return static_cast<Derived *>(this)->tick_impl(engine);
  }
};

class ExecutionAlgo {
public:
  virtual ~ExecutionAlgo() = default;
  virtual bool tick(ExecutionEngine &engine) = 0;
  Order parent_order;
};

class TWAPAlgo : public ExecutionAlgo, public AlgoCRTP<TWAPAlgo> {
  int64_t duration_sec;
  int64_t start_time_ns;
  int64_t last_slice_time_ns = 0;
  int total_qty;
  int filled_qty = 0;

public:
  TWAPAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);

private:
  void dispatch_slice(int qty, ExecutionEngine &engine);
};

class TrailingStopAlgo : public ExecutionAlgo,
                         public AlgoCRTP<TrailingStopAlgo> {
  Price best_price;
  Price trail_amount;
  std::string active_order_id;
  int64_t last_log_time_ns = 0;
  bool activated = false;

public:
  TrailingStopAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

class PegAlgo : public ExecutionAlgo, public AlgoCRTP<PegAlgo> {
  Price offset;
  ReferencePrice ref;
  Price last_quoted_price = Price(-1);
  int64_t last_update_time_ns = 0;
  std::string active_order_id;

public:
  PegAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

class VWAPAlgo : public ExecutionAlgo, public AlgoCRTP<VWAPAlgo> {
  double participation_rate;
  int total_qty;
  int filled_qty = 0;
  int64_t last_market_volume = -1;
  int64_t last_slice_time_ns = 0;

public:
  VWAPAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

class ArbitrageAlgo : public ExecutionAlgo, public AlgoCRTP<ArbitrageAlgo> {
  MarketId m1;
  MarketId m2;
  const MarketBackend *b1;
  const MarketBackend *b2;
  Price min_profit;
  int quantity;
  bool active = true;

public:
  ArbitrageAlgo(MarketId m1, const MarketBackend *b1, MarketId m2,
                const MarketBackend *b2, Price min_profit, int qty);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

class MarketMakerAlgo : public ExecutionAlgo, public AlgoCRTP<MarketMakerAlgo> {
  Price spread;
  ReferencePrice ref;
  std::string bid_id;
  std::string ask_id;
  Price last_ref_price = Price(-1);

public:
  MarketMakerAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

class SORAlgo : public ExecutionAlgo, public AlgoCRTP<SORAlgo> {
  const MarketBackend *b1;
  const MarketBackend *b2;
  int total_qty;
  bool active = true;

public:
  SORAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override { return tick_impl(engine); }
  bool tick_impl(ExecutionEngine &engine);
};

} // namespace bop
