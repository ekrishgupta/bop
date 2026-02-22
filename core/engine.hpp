#pragma once

#include "core.hpp"
#include "logic.hpp"

#include <initializer_list>

namespace bop {

struct ExecutionEngine {};

// Global instance for testing the syntax
} // namespace bop

extern bop::ExecutionEngine LiveExchange;

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &) { (void)o; }

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
