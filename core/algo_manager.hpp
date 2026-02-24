#pragma once

#include "algo.hpp"
#include <memory>
#include <mutex>
#include <vector>

namespace bop {

class ExecutionStrategy {
public:
  virtual ~ExecutionStrategy() = default;
  virtual bool tick(ExecutionEngine &engine) = 0;
};

class AlgoManager {
  std::vector<std::unique_ptr<ExecutionAlgo>> active_algos;
  std::vector<std::unique_ptr<ExecutionStrategy>> active_strategies;
  std::mutex mtx;
  std::vector<std::unique_ptr<ExecutionAlgo>> pending_algos;
  std::vector<std::unique_ptr<ExecutionStrategy>> pending_strategies;

public:
  void submit(const Order &o) {
    std::lock_guard<std::mutex> lock(mtx);
    if (o.algo_type == AlgoType::TWAP) {
      pending_algos.push_back(std::make_unique<TWAPAlgo>(o));
    } else if (o.algo_type == AlgoType::Trailing) {
      pending_algos.push_back(std::make_unique<TrailingStopAlgo>(o));
    } else if (o.algo_type == AlgoType::Peg) {
      pending_algos.push_back(std::make_unique<PegAlgo>(o));
    } else if (o.algo_type == AlgoType::VWAP) {
      pending_algos.push_back(std::make_unique<VWAPAlgo>(o));
    }
  }

  void submit_strategy(std::unique_ptr<ExecutionStrategy> s) {
    std::lock_guard<std::mutex> lock(mtx);
    pending_strategies.push_back(std::move(s));
  }

  void tick(ExecutionEngine &engine) {
    std::lock_guard<std::mutex> lock(mtx);
    
    // 1. Move pending to active
    if (!pending_algos.empty()) {
      for (auto &a : pending_algos)
        active_algos.push_back(std::move(a));
      pending_algos.clear();
    }
    if (!pending_strategies.empty()) {
      for (auto &s : pending_strategies)
        active_strategies.push_back(std::move(s));
      pending_strategies.clear();
    }

    // 2. Tick Algos
    for (auto it = active_algos.begin(); it != active_algos.end();) {
      try {
        if ((*it)->tick(engine)) {
          it = active_algos.erase(it);
        } else {
          ++it;
        }
      } catch (const std::exception &e) {
        std::cerr << "[ALGO] Error ticking algorithm: " << e.what() << std::endl;
        it = active_algos.erase(it);
      }
    }

    // 3. Tick Strategies
    for (auto it = active_strategies.begin(); it != active_strategies.end();) {
      try {
        if ((*it)->tick(engine)) {
          it = active_strategies.erase(it);
        } else {
          ++it;
        }
      } catch (const std::exception &e) {
        std::cerr << "[STRATEGY] Error ticking strategy: " << e.what()
                  << std::endl;
        it = active_strategies.erase(it);
      }
    }
  }

  size_t active_count() {
    std::lock_guard<std::mutex> lock(mtx);
    return active_algos.size() + active_strategies.size();
  }
};

extern AlgoManager GlobalAlgoManager;

} // namespace bop
