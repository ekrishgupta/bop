#pragma once

#include "core.hpp"
#include "algo.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <functional>

namespace bop {

class ExecutionStrategy {
public:
  virtual ~ExecutionStrategy() = default;
  virtual bool tick(ExecutionEngine &engine) = 0;
  virtual void on_market_event(ExecutionEngine &engine, MarketId m, Price p, int64_t q) {}
  virtual void on_execution_event(ExecutionEngine &engine, const std::string& id, OrderStatus s) {}
};

class EventStrategy : public ExecutionStrategy {
    MarketId target_m;
    std::function<void(ExecutionEngine&)> action;
public:
    EventStrategy(MarketId m, std::function<void(ExecutionEngine&)> a) : target_m(m), action(a) {}
    bool tick(ExecutionEngine&) override { return false; } // Persistent
    void on_market_event(ExecutionEngine &engine, MarketId m, Price, int64_t) override {
        if (m.hash == target_m.hash) {
            action(engine);
        }
    }
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
    } else if (o.algo_type == AlgoType::Arbitrage) {
        auto data = std::get<ArbData>(o.algo_params);
        pending_algos.push_back(std::make_unique<ArbitrageAlgo>(
            o.market, o.backend, data.m2, data.b2, data.min_profit, o.quantity));
    }
  }

  void submit_strategy(std::unique_ptr<ExecutionStrategy> s) {
    std::lock_guard<std::mutex> lock(mtx);
    pending_strategies.push_back(std::move(s));
  }

  void tick(ExecutionEngine &engine) {
    std::lock_guard<std::mutex> lock(mtx);
    
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

    for (auto it = active_algos.begin(); it != active_algos.end();) {
      try {
        if ((*it)->tick(engine)) {
          it = active_algos.erase(it);
        } else {
          ++it;
        }
      } catch (...) {
        it = active_algos.erase(it);
      }
    }

    for (auto it = active_strategies.begin(); it != active_strategies.end();) {
      try {
        if ((*it)->tick(engine)) {
          it = active_strategies.erase(it);
        } else {
          ++it;
        }
      } catch (...) {
        it = active_strategies.erase(it);
      }
    }
  }

  size_t active_count() {
    std::lock_guard<std::mutex> lock(mtx);
    return active_algos.size() + active_strategies.size();
  }

  void broadcast_market_event(ExecutionEngine &engine, MarketId m, Price p, int64_t q) {
      std::lock_guard<std::mutex> lock(mtx);
      for (auto& s : active_strategies) s->on_market_event(engine, m, p, q);
  }

  void broadcast_execution_event(ExecutionEngine &engine, const std::string& id, OrderStatus s) {
      std::lock_guard<std::mutex> lock(mtx);
      for (auto& strat : active_strategies) strat->on_execution_event(engine, id, s);
  }
};

extern AlgoManager GlobalAlgoManager;

} // namespace bop
