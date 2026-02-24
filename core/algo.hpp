#pragma once

#include "core.hpp"
#include "market_base.hpp"
#include <chrono>
#include <memory>

namespace bop {

struct ExecutionEngine;

class ExecutionAlgo {
public:
  virtual ~ExecutionAlgo() = default;
  virtual bool tick(ExecutionEngine &engine) = 0;
  Order parent_order;
};

class TWAPAlgo : public ExecutionAlgo {
  int64_t duration_sec;
  int64_t start_time_ns;
  int64_t last_slice_time_ns = 0;
  int total_qty;
  int filled_qty = 0;

public:
  TWAPAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;

private:
  void dispatch_slice(int qty, ExecutionEngine &engine);
};

class TrailingStopAlgo : public ExecutionAlgo {
  Price best_price;
  Price trail_amount;
  std::string active_order_id;
  int64_t last_log_time_ns = 0;
  bool activated = false;

public:
  TrailingStopAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;
};

class PegAlgo : public ExecutionAlgo {
  Price offset;
  ReferencePrice ref;
  Price last_quoted_price = Price(-1);
  int64_t last_update_time_ns = 0;
  std::string active_order_id;

public:
  PegAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;
};

class VWAPAlgo : public ExecutionAlgo {
  double participation_rate;
  int total_qty;
  int filled_qty = 0;
  int64_t last_market_volume = -1;
  int64_t last_slice_time_ns = 0;

public:
  VWAPAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;
};

class ArbitrageAlgo : public ExecutionAlgo {
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
  bool tick(ExecutionEngine &engine) override;
};

} // namespace bop
