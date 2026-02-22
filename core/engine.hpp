#pragma once

#include "core.hpp"
#include "logic.hpp"

namespace bop {

struct ExecutionEngine {};

// Global instance for testing the syntax
} // namespace bop

extern bop::ExecutionEngine LiveExchange;

// Final Dispatch Operators
inline void operator>>(const bop::Order &o, bop::ExecutionEngine &) { (void)o; }

template <typename Tag>
inline void operator>>(const bop::ConditionalOrder<Tag> &co,
                       bop::ExecutionEngine &) {
  (void)co;
}

inline void operator>>(const bop::OCOOrder &oco, bop::ExecutionEngine &) {
  (void)oco;
}
