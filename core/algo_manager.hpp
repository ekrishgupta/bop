#pragma once

#include "algo.hpp"
#include "core.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace bop {

class ExecutionEngine;

template <typename Derived> class StrategyCRTP {
public:
  bool tick(ExecutionEngine &engine) {
    return static_cast<Derived *>(this)->tick_impl(engine);
  }
  void on_market_event(ExecutionEngine &engine, MarketId m, Price p,
                       int64_t q) {
    static_cast<Derived *>(this)->on_market_event_impl(engine, m, p, q);
  }
  void on_execution_event(ExecutionEngine &engine, const std::string &id,
                          OrderStatus s) {
    static_cast<Derived *>(this)->on_execution_event_impl(engine, id, s);
  }
};

class ExecutionStrategy {
public:
  virtual ~ExecutionStrategy() = default;
  virtual bool tick(ExecutionEngine &engine) = 0;
  virtual void on_market_event(ExecutionEngine &engine, MarketId m, Price p,
                               int64_t q) {}
  virtual void on_execution_event(ExecutionEngine &engine,
                                  const std::string &id, OrderStatus s) {}
};

class EventStrategy : public ExecutionStrategy,
                      public StrategyCRTP<EventStrategy> {
  MarketId target_m;
  std::function<void(ExecutionEngine &)> action;

public:
  EventStrategy(MarketId m, std::function<void(ExecutionEngine &)> a)
      : target_m(m), action(a) {}
  bool tick(ExecutionEngine &) override { return false; }
  bool tick_impl(ExecutionEngine &) { return false; }

  void on_market_event(ExecutionEngine &engine, MarketId m, Price p,
                       int64_t q) override {
    on_market_event_impl(engine, m, p, q);
  }

  void on_market_event_impl(ExecutionEngine &engine, MarketId m, Price,
                            int64_t) {
    if (m.hash == target_m.hash) {
      action(engine);
    }
  }

  void on_execution_event_impl(ExecutionEngine &, const std::string &,
                               OrderStatus) {}
};

class AlgoManager {
  // Concrete containers for high-performance static dispatch
  std::vector<std::unique_ptr<TWAPAlgo>> twap_algos;
  std::vector<std::unique_ptr<TrailingStopAlgo>> trailing_algos;
  std::vector<std::unique_ptr<PegAlgo>> peg_algos;
  std::vector<std::unique_ptr<VWAPAlgo>> vwap_algos;
  std::vector<std::unique_ptr<ArbitrageAlgo>> arb_algos;
  std::vector<std::unique_ptr<MarketMakerAlgo>> mm_algos;

  // Generic strategies still use the virtual interface but are optimized via
  // CRTP where possible
  std::vector<std::unique_ptr<ExecutionStrategy>> active_strategies;

  std::mutex mtx;

  // Pending queues
  std::vector<Order> pending_orders;
  std::vector<std::unique_ptr<ExecutionStrategy>> pending_strategies;

public:
  void submit(const Order &o) {
    std::lock_guard<std::mutex> lock(mtx);
    pending_orders.push_back(o);
  }

  void submit_strategy(std::unique_ptr<ExecutionStrategy> s) {
    std::lock_guard<std::mutex> lock(mtx);
    pending_strategies.push_back(std::move(s));
  }

  void tick(ExecutionEngine &engine) {
    std::lock_guard<std::mutex> lock(mtx);

    // Process pending orders with static dispatch creation
    if (!pending_orders.empty()) {
      for (const auto &o : pending_orders) {
        switch (o.algo_type) {
        case AlgoType::TWAP:
          twap_algos.push_back(std::make_unique<TWAPAlgo>(o));
          break;
        case AlgoType::Trailing:
          trailing_algos.push_back(std::make_unique<TrailingStopAlgo>(o));
          break;
        case AlgoType::Peg:
          peg_algos.push_back(std::make_unique<PegAlgo>(o));
          break;
        case AlgoType::VWAP:
          vwap_algos.push_back(std::make_unique<VWAPAlgo>(o));
          break;
        case AlgoType::Arbitrage: {
          auto data = std::get<ArbData>(o.algo_params);
          arb_algos.push_back(std::make_unique<ArbitrageAlgo>(
              o.market, o.backend, data.m2, data.b2, data.min_profit,
              o.quantity));
          break;
        }
        case AlgoType::MarketMaker:
          mm_algos.push_back(std::make_unique<MarketMakerAlgo>(o));
          break;
        default:
          break;
        }
      }
      pending_orders.clear();
    }

    if (!pending_strategies.empty()) {
      for (auto &s : pending_strategies)
        active_strategies.push_back(std::move(s));
      pending_strategies.clear();
    }

    // Static Dispatch Hot Path (No Virtual Calls)
    tick_container(twap_algos, engine);
    tick_container(trailing_algos, engine);
    tick_container(peg_algos, engine);
    tick_container(vwap_algos, engine);
    tick_container(arb_algos, engine);
    tick_container(mm_algos, engine);

    // Optimized Strategy Loop
    for (size_t i = 0; i < active_strategies.size();) {
      if (active_strategies[i]->tick(engine)) {
        std::swap(active_strategies[i], active_strategies.back());
        active_strategies.pop_back();
      } else {
        ++i;
      }
    }
  }

  size_t active_count() {
    std::lock_guard<std::mutex> lock(mtx);
    return twap_algos.size() + trailing_algos.size() + peg_algos.size() +
           vwap_algos.size() + arb_algos.size() + mm_algos.size() +
           active_strategies.size();
  }

  void broadcast_market_event(ExecutionEngine &engine, MarketId m, Price p,
                              int64_t q) {
    std::lock_guard<std::mutex> lock(mtx);
    // Hot path event broadcasting
    for (auto &s : active_strategies)
      s->on_market_event(engine, m, p, q);
  }

  void broadcast_execution_event(ExecutionEngine &engine, const std::string &id,
                                 OrderStatus s) {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto &strat : active_strategies)
      strat->on_execution_event(engine, id, s);
  }

private:
  template <typename T>
  void tick_container(std::vector<std::unique_ptr<T>> &container,
                      ExecutionEngine &engine) {
    for (size_t i = 0; i < container.size();) {
      // Direct call via CRTP/Static dispatch
      if (container[i]->tick_impl(engine)) {
        std::swap(container[i], container.back());
        container.pop_back();
      } else {
        ++i;
      }
    }
  }
};

extern AlgoManager GlobalAlgoManager;

} // namespace bop
