#include "algo.hpp"
#include "algo_manager.hpp"
#include "engine.hpp"
#include <iostream>

namespace bop {

AlgoManager GlobalAlgoManager;

// TWAPAlgo Implementation
TWAPAlgo::TWAPAlgo(const Order &o) {
  parent_order = o;
  duration_sec = std::get<int64_t>(o.algo_params);
  total_qty = o.quantity;
  start_time_ns =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

bool TWAPAlgo::tick(ExecutionEngine &engine) {
  int64_t now_ns =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  double elapsed_sec = (now_ns - start_time_ns) / 1e9;

  if (elapsed_sec >= duration_sec) {
    int remaining = total_qty - filled_qty;
    if (remaining > 0) {
      dispatch_slice(remaining, engine);
    }
    std::cout << "[ALGO] TWAP Completed for " << parent_order.market.ticker
              << std::endl;
    return true;
  }

  double target_qty = (elapsed_sec / (double)duration_sec) * total_qty;
  int to_fill = static_cast<int>(target_qty) - filled_qty;

  if (to_fill > 0) {
    dispatch_slice(to_fill, engine);
    filled_qty += to_fill;
  }
  return false;
}

void TWAPAlgo::dispatch_slice(int qty, ExecutionEngine &engine) {
  Order slice = parent_order;
  slice.quantity = qty;
  slice.algo_type = AlgoType::None;

  if (slice.backend) {
    std::cout << "[ALGO] TWAP Slice: " << qty << " shares to "
              << slice.backend->name() << std::endl;
    slice.backend->create_order(slice);
  }
}

// TrailingStopAlgo Implementation
TrailingStopAlgo::TrailingStopAlgo(const Order &o) {
  parent_order = o;
  trail_amount = Price(std::get<int64_t>(o.algo_params));
  best_price = Price(0);
}

bool TrailingStopAlgo::tick(ExecutionEngine &engine) {
  Price current_price =
      engine.get_price(parent_order.market, parent_order.outcome_yes);

  if (!activated) {
    std::cout << "[ALGO] Trailing Stop Activated for "
              << parent_order.market.ticker << " at " << current_price
              << std::endl;
    best_price = current_price;
    activated = true;
    return false;
  }

  bool price_improved = parent_order.is_buy ? (current_price < best_price)
                                            : (current_price > best_price);

  if (price_improved || best_price.raw == 0) {
    best_price = current_price;
    std::cout << "[ALGO] Trailing Stop Updated Best Price for "
              << parent_order.market.ticker << ": " << best_price << std::endl;
  }

  Price stop_price = parent_order.is_buy ? (best_price + trail_amount)
                                         : (best_price - trail_amount);

  // Log every 4 ticks to reduce spam
  static int tick_count = 0;
  if (++tick_count % 4 == 0) {
    std::cout << "[ALGO] Trailing Stop Tick [" << parent_order.market.ticker
              << "] Price: " << current_price << " Stop: " << stop_price
              << " Best: " << best_price << std::endl;
  }

  bool triggered = parent_order.is_buy ? (current_price >= stop_price)
                                       : (current_price <= stop_price);

  if (triggered) {
    std::cout << "[ALGO] Trailing Stop Triggered at " << current_price
              << " (Best: " << best_price << ")" << std::endl;
    Order market_order = parent_order;
    market_order.algo_type = AlgoType::None;
    market_order.price = Price(0);
    if (market_order.backend) {
      market_order.backend->create_order(market_order);
    }
    return true;
  }
  return false;
}

// PegAlgo Implementation
PegAlgo::PegAlgo(const Order &o) {
  parent_order = o;
  auto data = std::get<PegData>(o.algo_params);
  offset = data.offset;
  ref = data.ref;
}

bool PegAlgo::tick(ExecutionEngine &engine) {
  bool is_bid = (ref == ReferencePrice::Bid);
  Price bbo_price = engine.get_depth(parent_order.market, is_bid);

  if (bbo_price.raw == 0)
    return false;

  Price target_price = bbo_price + offset;

  if (target_price != last_quoted_price) {
    if (!active_order_id.empty() && parent_order.backend) {
      parent_order.backend->cancel_order(active_order_id);
    }

    Order slice = parent_order;
    slice.price = target_price;
    slice.algo_type = AlgoType::None;

    if (slice.backend) {
      std::cout << "[ALGO] Pegging " << parent_order.market.ticker << " to "
                << target_price << std::endl;
      active_order_id = slice.backend->create_order(slice);
    }
    last_quoted_price = target_price;
  }
  return false;
}

} // namespace bop
