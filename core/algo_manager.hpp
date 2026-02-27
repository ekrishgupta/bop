#pragma once

#include "algo.hpp"
#include "core.hpp"
#include <array>
#include <functional>
#include <memory>
#include <memory_resource>
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
  // Genetic strategies use PMR for zero-allocation lifecycle
  alignas(std::max_align_t) std::array<std::byte, 1024 * 1024 * 4> pool_buffer;
  std::pmr::monotonic_buffer_resource pool_resource;

  // Concrete containers using PMR for zero-allocation resizing
  std::pmr::vector<TWAPAlgo> twap_algos;
  std::pmr::vector<TrailingStopAlgo> trailing_algos;
  std::pmr::vector<PegAlgo> peg_algos;
  std::pmr::vector<VWAPAlgo> vwap_algos;
  std::pmr::vector<ArbitrageAlgo> arb_algos;
  std::pmr::vector<MarketMakerAlgo> mm_algos;
  std::pmr::vector<SORAlgo> sor_algos;

  std::pmr::vector<ExecutionStrategy *> active_strategies;

  std::mutex mtx;

  // Pending queues
  std::pmr::vector<Order> pending_orders;
  std::pmr::vector<ExecutionStrategy *> pending_strategies;

public:
  AlgoManager()
      : pool_resource(pool_buffer.data(), pool_buffer.size()),
        twap_algos(&pool_resource), trailing_algos(&pool_resource),
        peg_algos(&pool_resource), vwap_algos(&pool_resource),
        arb_algos(&pool_resource), mm_algos(&pool_resource),
        sor_algos(&pool_resource), active_strategies(&pool_resource),
        pending_orders(&pool_resource), pending_strategies(&pool_resource) {
    twap_algos.reserve(1024);
    trailing_algos.reserve(1024);
    peg_algos.reserve(1024);
    vwap_algos.reserve(1024);
    arb_algos.reserve(1024);
    mm_algos.reserve(1024);
    sor_algos.reserve(1024);
    pending_orders.reserve(1024);
    active_strategies.reserve(256);
    pending_strategies.reserve(256);
  }

  void submit(const Order &o) {
    std::lock_guard<std::mutex> lock(mtx);
    pending_orders.push_back(o);
  }

  template <typename T, typename... Args> T *create_strategy(Args &&...args) {
    std::lock_guard<std::mutex> lock(mtx);
    std::pmr::polymorphic_allocator<T> alloc(&pool_resource);
    T *s = alloc.new_object<T>(std::forward<Args>(args)...);
    pending_strategies.push_back(s);
    return s;
  }

  // Deprecated: uses heap allocation. Migrating to create_strategy.
  void submit_strategy(std::unique_ptr<ExecutionStrategy> s) {
    std::lock_guard<std::mutex> lock(mtx);
    // We still support this by taking ownership, but it's not zero-allocation
    // unless the caller uses PMR.
    pending_strategies.push_back(s.release());
  }

  void tick(ExecutionEngine &engine) {
    std::lock_guard<std::mutex> lock(mtx);

    // Process pending orders with static dispatch creation
    if (!pending_orders.empty()) {
      for (const auto &o : pending_orders) {
        switch (o.algo_type) {
        case AlgoType::TWAP:
          twap_algos.emplace_back(o);
          break;
        case AlgoType::Trailing:
          trailing_algos.emplace_back(o);
          break;
        case AlgoType::Peg:
          peg_algos.emplace_back(o);
          break;
        case AlgoType::VWAP:
          vwap_algos.emplace_back(o);
          break;
        case AlgoType::Arbitrage: {
          auto data = std::get<ArbData>(o.algo_params);
          arb_algos.emplace_back(o.market, o.backend, data.m2, data.b2,
                                 data.min_profit, o.quantity);
          break;
        }
        case AlgoType::MarketMaker:
          mm_algos.emplace_back(o);
          break;
        case AlgoType::SOR:
          sor_algos.emplace_back(o);
          break;
        default:
          break;
        }
      }
      pending_orders.clear();
    }

    if (!pending_strategies.empty()) {
      for (auto s : pending_strategies)
        active_strategies.push_back(s);
      pending_strategies.clear();
    }

    // Static Dispatch Hot Path (No Virtual Calls)
    tick_container(twap_algos, engine);
    tick_container(trailing_algos, engine);
    tick_container(peg_algos, engine);
    tick_container(vwap_algos, engine);
    tick_container(arb_algos, engine);
    tick_container(mm_algos, engine);
    tick_container(sor_algos, engine);

    // Optimized Strategy Loop
    for (size_t i = 0; i < active_strategies.size();) {
      if (active_strategies[i]->tick(engine)) {
        // Since we use PMR, we manually call the destructor
        active_strategies[i]->~ExecutionStrategy();
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
           sor_algos.size() + active_strategies.size();
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
  void tick_container(std::vector<T> &container, ExecutionEngine &engine) {
    for (size_t i = 0; i < container.size();) {
      // Direct call via CRTP/Static dispatch
      if (container[i].tick_impl(engine)) {
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
