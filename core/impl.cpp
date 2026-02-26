#include "algo.hpp"
#include "algo_manager.hpp"
#include "engine.hpp"
#include "order_tracker.hpp"
#include "streaming_backend.hpp"
#include <iostream>
namespace bop {

AlgoManager GlobalAlgoManager;
OrderTracker GlobalOrderTracker;

// --- ExecutionEngine ---

void ExecutionEngine::run() {
  is_running = true;
  while (is_running) {
    GlobalAlgoManager.tick(*this);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void ExecutionEngine::execute_order(const Order &o) {
  if (o.backend) {
    std::string id = o.backend->create_order(o);
    if (!id.empty() && id != "error")
      track_order(id, o);
  }
}

void ExecutionEngine::execute_cancel(const std::string &id) {
  auto record = order_store.find(id);
  if (record && record->order.backend) {
    if (record->order.backend->cancel_order(id)) {
      update_order_status(id, OrderStatus::Cancelled);
    }
  }
}

void ExecutionEngine::execute_batch(const std::vector<Order> &orders) {
  for (const auto &o : orders)
    execute_order(o);
}

void ExecutionEngine::track_order(const std::string &id, const Order &o) {
  db.log_order(id, o);
  order_store.track(id, o);
}

void ExecutionEngine::update_order_status(const std::string &id,
                                          OrderStatus status) {
  db.log_status(id, status);
  order_store.update_status(id, status);
}

void ExecutionEngine::add_order_fill(const std::string &id, int qty,
                                     Price price) {
  db.log_fill(id, qty, price);
  order_store.add_fill(id, qty, price);
  int64_t simulated_loss = (price.raw * qty) / 100;
  current_daily_pnl_raw -= simulated_loss;
  std::cout << "[ENGINE] Fill recorded for " << id << ": " << qty << " @ "
            << price << std::endl;
  check_kill_switch();
}

// --- LiveExecutionEngine ---

LiveExecutionEngine::~LiveExecutionEngine() {
  stop();
  if (sync_thread.joinable())
    sync_thread.join();
  tick_cv.notify_all();
}

int64_t LiveExecutionEngine::get_position(MarketId market) const {
  auto state = current_state.load();
  auto it = state->positions.find(market.hash);
  return (it != state->positions.end()) ? it->second : 0;
}

Price LiveExecutionEngine::get_balance() const {
  return current_state.load()->balance;
}

double
LiveExecutionEngine::get_portfolio_metric(PortfolioQuery::Metric metric) const {
  auto state = current_state.load();
  std::unordered_map<uint32_t, double> volatilities;
  for (auto const &[hash, tracker] : market_volatility) {
    volatilities[hash] = tracker.current_vol;
  }

  auto pg = const_cast<GreekEngine &>(greek_engine)
                .calculate_portfolio_greeks(state->positions, backends_,
                                            volatilities);

  switch (metric) {
  case PortfolioQuery::Metric::TotalDelta:
    return pg.total_delta;
  case PortfolioQuery::Metric::TotalGamma:
    return pg.total_gamma;
  case PortfolioQuery::Metric::TotalTheta:
    return pg.total_theta;
  case PortfolioQuery::Metric::TotalVega:
    return pg.total_vega;
  case PortfolioQuery::Metric::NetExposure:
    return get_exposure().to_double();
  case PortfolioQuery::Metric::PortfolioValue:
    return state->balance.to_double();
  default:
    return 0.0;
  }
}

Price LiveExecutionEngine::get_exposure() const {
  return current_state.load()->exposure;
}
Price LiveExecutionEngine::get_pnl() const { return current_state.load()->pnl; }

void LiveExecutionEngine::run() {
  is_running = true;
  sync_state();
  sync_thread = std::thread([this]() {
    while (is_running) {
      sync_state();
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  });

  while (is_running) {
    {
      std::unique_lock<std::mutex> lock(tick_mtx);
      tick_cv.wait_for(lock, std::chrono::milliseconds(100));
    }
    if (!is_running)
      break;
    GlobalAlgoManager.tick(*this);
    check_kill_switch();
  }
}

void LiveExecutionEngine::sync_state() {
  Price total_balance(0);
  Price total_exposure(0);
  std::unordered_map<uint32_t, int64_t> new_positions;
  for (auto b : backends_) {
    total_balance = total_balance + b->get_balance();
    std::string pos_json = b->get_positions();
    try {
      auto j = nlohmann::json::parse(pos_json);
      if (j.is_array()) {
        for (const auto &p : j) {
          std::string ticker;
          if (p.contains("asset_id"))
            ticker = p["asset_id"];
          if (!ticker.empty() && p.contains("size")) {
            int64_t size = std::stoll(p["size"].get<std::string>());
            new_positions[fnv1a(ticker.c_str())] += size;

            // Calculate exposure
            Price mid = b->get_price(MarketId(ticker), true);
            if (mid.raw > 0) {
              total_exposure.raw += std::abs(size) * mid.raw;
            }
          }
        }
      }
    } catch (...) {
    }
  }
  auto new_state = std::make_shared<LiveEngineState>();
  new_state->balance = total_balance;
  new_state->positions = std::move(new_positions);
  new_state->exposure = total_exposure;
  new_state->pnl = Price(current_daily_pnl_raw.load());

  current_state.store(std::move(new_state));
}

// --- StreamingMarketBackend ---

void StreamingMarketBackend::update_price(MarketId market, Price yes,
                                          Price no) {
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    price_cache_[market.hash] = {yes, no};
  }
  if (engine_)
    engine_->trigger_tick();
}

void StreamingMarketBackend::update_orderbook(MarketId market,
                                              const OrderBook &ob) {
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    orderbook_cache_[market.hash] = ob;
  }
  if (engine_)
    engine_->trigger_tick();
}

void StreamingMarketBackend::update_orderbook_incremental(
    MarketId market, bool is_bid, const OrderBookLevel &level) {
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto &ob = orderbook_cache_[market.hash];
    auto &target = is_bid ? ob.bids : ob.asks;

    auto it = std::find_if(target.begin(), target.end(),
                           [&](const OrderBookLevel &l) {
                             return l.price.raw == level.price.raw;
                           });

    if (it != target.end()) {
      // level.quantity is the DELTA in Kalshi, but usually it's the NEW total.
      // For standard incremental feeds, it's the NEW quantity.
      // If it's 0, the level is removed.
      if (level.quantity <= 0) {
        target.erase(it);
      } else {
        it->quantity = level.quantity;
      }
    } else if (level.quantity > 0) {
      target.push_back(level);
      // Re-sort bids descending, asks ascending
      if (is_bid) {
        std::sort(target.begin(), target.end(),
                  [](const auto &a, const auto &b) {
                    return a.price.raw > b.price.raw;
                  });
      } else {
        std::sort(target.begin(), target.end(),
                  [](const auto &a, const auto &b) {
                    return a.price.raw < b.price.raw;
                  });
      }
    }
  }
  if (engine_)
    engine_->trigger_tick();
}

void StreamingMarketBackend::notify_fill(const std::string &id, int qty,
                                         Price price) {
  if (engine_)
    engine_->add_order_fill(id, qty, price);
}

void StreamingMarketBackend::notify_status(const std::string &id,
                                           OrderStatus status) {
  if (engine_)
    engine_->update_order_status(id, status);
}

// --- Strategies ---

template class PersistentConditionalStrategy<Condition<PriceTag>>;
template class PersistentConditionalStrategy<Condition<PositionTag>>;
template class PersistentConditionalStrategy<Condition<BalanceTag>>;
template class PersistentConditionalStrategy<RelativeCondition<PriceTag>>;

// --- Dispatch Operators ---

void operator>>(const Order &o, ExecutionEngine &engine) {
  Order order_to_dispatch = o;
  if (engine.limits.dynamic_sizing_enabled) {
    order_to_dispatch.quantity = engine.calculate_dynamic_size(o);
  }
  if (!engine.check_risk(order_to_dispatch))
    return;
  if (order_to_dispatch.algo_type != AlgoType::None) {
    GlobalAlgoManager.submit(order_to_dispatch);
    return;
  }
  // Synchronous execution for now to fix backtest visibility
  engine.execute_order(order_to_dispatch);
}

void operator>>(std::initializer_list<Order> batch, ExecutionEngine &engine) {
  if (batch.size() == 0)
    return;
  std::vector<Order> orders(batch);
  engine.execute_batch(orders);
}

void operator>>(const OCOOrder &oco, ExecutionEngine &engine) {
  oco.order1 >> engine;
  oco.order2 >> engine;
}

Order operator>>(MarketBoundQuote q, ExecutionEngine &engine) {
  Order o;
  o.market = q.market;
  o.quantity = q.quantity;
  o.backend = q.backend;
  o.algo_type = AlgoType::MarketMaker;
  o.algo_params = MarketMakerData{q.spread, q.ref};
  o.creation_timestamp_ns = q.timestamp_ns;

  o >> engine;
  return o;
}

} // namespace bop
