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
                       bop::ExecutionEngine &) {
  (void)batch;
}

template <typename Tag>
inline void operator>>(const bop::ConditionalOrder<Tag> &co,
                       bop::ExecutionEngine &) {
  (void)co;
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
  }
  return false;
}

} // namespace bop
