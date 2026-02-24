#pragma once

#include "algo.hpp"
#include <memory>
#include <mutex>
#include <vector>

namespace bop {

class AlgoManager {
  std::vector<std::unique_ptr<ExecutionAlgo>> active_algos;
  std::mutex mtx;

public:
  void submit(const Order &o) {
    std::lock_guard<std::mutex> lock(mtx);
    if (o.algo_type == AlgoType::TWAP) {
      active_algos.push_back(std::make_unique<TWAPAlgo>(o));
    } else if (o.algo_type == AlgoType::Trailing) {
      active_algos.push_back(std::make_unique<TrailingStopAlgo>(o));
    } else if (o.algo_type == AlgoType::Peg) {
      active_algos.push_back(std::make_unique<PegAlgo>(o));
    }
  }

  void tick(ExecutionEngine &engine) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto it = active_algos.begin(); it != active_algos.end();) {
      if ((*it)->tick(engine)) {
        it = active_algos.erase(it);
      } else {
        ++it;
      }
    }
  }

  size_t active_count() {
    std::lock_guard<std::mutex> lock(mtx);
    return active_algos.size();
  }
};

extern AlgoManager GlobalAlgoManager;

} // namespace bop
