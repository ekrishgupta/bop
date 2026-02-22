#pragma once

#include "core.hpp"
#include <string>

namespace bop {

struct MarketBackend {
  virtual ~MarketBackend() = default;
  virtual std::string name() const = 0;
  virtual int64_t get_price(MarketId market, bool outcome_yes) const = 0;
  virtual int64_t get_depth(MarketId market, bool is_bid) const = 0;
};

} // namespace bop
