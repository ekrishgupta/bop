#pragma once

#include "core.hpp"
#include "logic.hpp"
#include <iostream>

#include <initializer_list>
#include <type_traits>

namespace bop {

struct ExecutionEngine {
  virtual int64_t get_position(MarketId market) const { return 0; }
  virtual int64_t get_balance() const { return 0; }
  virtual int64_t get_exposure() const { return 0; }
  virtual int64_t get_pnl() const { return 0; }
  virtual int64_t get_depth(MarketId market, bool is_bid) const { return 0; }
};

// Global instance for testing the syntax
} // namespace bop

extern bop::ExecutionEngine LiveExchange;

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &) {
  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t latency = now - o.creation_timestamp_ns;
  std::cout << "[LATENCY] Order reached engine in " << latency << " ns."
            << std::endl;
}

inline void operator>>(std::initializer_list<bop::Order> batch,
                       bop::ExecutionEngine &engine) {
  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  std::cout << "[BATCH] Processing batch of " << batch.size() << " orders."
            << std::endl;
  for (const auto &o : batch) {
    uint64_t latency = now - o.creation_timestamp_ns;
    std::cout << "  -> [LATENCY] Order reached engine in " << latency << " ns."
              << std::endl;
  }
}

template <typename Tag>
inline void operator>>(const bop::ConditionalOrder<Tag> &co,
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

inline void operator>>(const bop::OCOOrder &oco, bop::ExecutionEngine &) {
  (void)oco;
}

namespace bop {

template <typename Tag, typename Q>
inline bool Condition<Tag, Q>::eval() const {
  if constexpr (std::is_same_v<Tag, PositionTag>) {
    int64_t val = LiveExchange.get_position(query.market);
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, BalanceTag>) {
    int64_t val = LiveExchange.get_balance();
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, ExposureTag>) {
    int64_t val = LiveExchange.get_exposure();
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, PnLTag>) {
    int64_t val = LiveExchange.get_pnl();
    return is_greater ? val > threshold : val < threshold;
  } else if constexpr (std::is_same_v<Tag, DepthTag>) {
    int64_t val = LiveExchange.get_depth(query.market, query.outcome_yes);
    return is_greater ? val > threshold : val < threshold;
  }
  return false;
}

} // namespace bop
