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
  bool activated = false;

public:
  TrailingStopAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;
};

class PegAlgo : public ExecutionAlgo {
  Price offset;
  ReferencePrice ref;
  Price last_quoted_price = Price(-1);
  std::string active_order_id;

public:
  PegAlgo(const Order &o);
  bool tick(ExecutionEngine &engine) override;
};

} // namespace bop
