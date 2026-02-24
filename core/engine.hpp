#pragma once

#include "core.hpp"
#include "logic.hpp"
#include <iostream>

#include <initializer_list>
#include <type_traits>

namespace bop {

struct ExecutionEngine {
  virtual int64_t get_position(MarketId market) const { return 0; }
  virtual Price get_balance() const { return Price(0); }
  virtual Price get_exposure() const { return Price(0); }
  virtual Price get_pnl() const { return Price(0); }
  virtual Price get_depth(MarketId market, bool is_bid) const {
    return Price(0);
  }
  virtual Price get_price(MarketId market, bool outcome_yes) const {
    return Price(0);
  }
};

} // namespace bop

extern bop::ExecutionEngine &LiveExchange;

#include "algo_manager.hpp"

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &engine) {
  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t latency = now - o.creation_timestamp_ns;

  if (o.algo_type != bop::AlgoType::None) {
    std::cout << "[ALGO] Registering " << (int)o.algo_type << " for "
              << o.market.ticker << std::endl;
    bop::GlobalAlgoManager.submit(o);
    return;
  }

  if (o.backend) {
    if (o.is_spread) {
      std::cout << "[BACKEND] Dispatching spread order (" << o.market.hash
                << " - " << o.market2.hash << ") to " << o.backend->name()
                << " (" << latency << " ns latency)" << std::endl;
    } else {
      std::cout << "[BACKEND] Dispatching to " << o.backend->name() << " ("
                << latency << " ns latency)" << std::endl;
    }
    o.backend->create_order(o);
  } else {
    if (o.is_spread) {
      std::cout << "[ENGINE] No backend bound for spread order ("
                << o.market.hash << " - " << o.market2.hash
                << "). Simulated latency: " << latency << " ns." << std::endl;
    } else {
      std::cout << "[ENGINE] No backend bound. Simulated latency: " << latency
                << " ns." << std::endl;
    }
  }
}

inline void operator>>(std::initializer_list<bop::Order> batch,
                       bop::ExecutionEngine &engine) {
  if (batch.size() == 0)
    return;

  const bop::MarketBackend *common_backend = batch.begin()->backend;
  bool all_same = true;
  for (const auto &o : batch) {
    if (o.backend != common_backend) {
      all_same = false;
      break;
    }
  }

  if (all_same && common_backend) {
    std::cout << "[BATCH] Dispatching " << batch.size() << " orders to "
              << common_backend->name() << std::endl;
    std::vector<bop::Order> orders(batch);
    common_backend->create_batch_orders(orders);
  } else {
    std::cout << "[BATCH] Heterogeneous batch. Dispatching individually..."
              << std::endl;
    for (const auto &o : batch) {
      o >> engine;
    }
  }
}

namespace bop {

template <typename Tag> inline bool RelativeCondition<Tag>::eval() const {
  if constexpr (std::is_same_v<Tag, PriceTag>) {
    Price l_val = left.backend
                      ? left.backend->get_price(left.market, left.outcome_yes)
                      : LiveExchange.get_price(left.market, left.outcome_yes);
    Price r_val =
        right.backend
            ? right.backend->get_price(right.market, right.outcome_yes)
            : LiveExchange.get_price(right.market, right.outcome_yes);
    return is_greater ? l_val > r_val : l_val < r_val;
  } else {
    int64_t l_val = 0;
    int64_t r_val = 0;
    return is_greater ? l_val > r_val : l_val < r_val;
  }
}

template <typename Tag, typename Q>
inline bool Condition<Tag, Q>::eval() const {
  if constexpr (std::is_same_v<Tag, PositionTag>) {
    int64_t val = LiveExchange.get_position(query.market);
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, BalanceTag>) {
    Price val = LiveExchange.get_balance();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, ExposureTag>) {
    Price val = LiveExchange.get_exposure();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, PnLTag>) {
    Price val = LiveExchange.get_pnl();
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, PriceTag>) {
    Price val = query.backend
                    ? query.backend->get_price(query.market, query.outcome_yes)
                    : LiveExchange.get_price(query.market, query.outcome_yes);
    return is_greater ? val.raw > threshold : val.raw < threshold;
  } else if constexpr (std::is_same_v<Tag, DepthTag>) {
    Price val = query.backend
                    ? query.backend->get_depth(query.market, query.outcome_yes)
                    : LiveExchange.get_depth(query.market, query.outcome_yes);
    return is_greater ? val.raw > threshold : val.raw < threshold;
  }
  return false;
}

} // namespace bop

template <typename T>
inline void operator>>(const bop::ConditionalOrder<T> &co,
                       bop::ExecutionEngine &engine) {
  if (co.condition.eval()) {
    std::cout << "[CONDITION] Passed conditional evaluation. Dispatching..."
              << std::endl;
    co.order >> engine;
  } else {
    std::cout << "[CONDITION] Failed conditional evaluation. Suppressing order."
              << std::endl;
  }
}

inline void operator>>(const bop::OCOOrder &oco, bop::ExecutionEngine &engine) {
  std::cout << "[OCO] Dispatching OCO pair..." << std::endl;
  oco.order1 >> engine;
  oco.order2 >> engine;
}
