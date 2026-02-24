#include "engine.hpp"
#include "streaming_backend.hpp"
#include "algo_manager.hpp"
#include "algo.hpp"
#include <iostream>

namespace bop {

AlgoManager GlobalAlgoManager;
OrderTracker GlobalOrderTracker;

// --- ExecutionEngine ---

void ExecutionEngine::run() {
    is_running = true;
    std::cout << "[ENGINE] Starting default responsive event loop..." << std::endl;
    while (is_running) {
      GlobalAlgoManager.tick(*this);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// --- LiveExecutionEngine ---

LiveExecutionEngine::~LiveExecutionEngine() {
    stop();
    if (sync_thread.joinable()) sync_thread.join();
    tick_cv.notify_all();
}

int64_t LiveExecutionEngine::get_position(MarketId market) const {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cached_positions.find(market.hash);
    return (it != cached_positions.end()) ? it->second : 0;
}

Price LiveExecutionEngine::get_balance() const {
    std::lock_guard<std::mutex> lock(mtx);
    return cached_balance;
}

Price LiveExecutionEngine::get_exposure() const {
    std::lock_guard<std::mutex> lock(mtx);
    Price total_exposure(0);
    return total_exposure;
}

Price LiveExecutionEngine::get_pnl() const {
    return Price(0);
}

void LiveExecutionEngine::run() {
    is_running = true;

    std::cout << "[LIVE ENGINE] Performing initial state sync..." << std::endl;
    sync_state();

    sync_thread = std::thread([this]() {
      while (is_running) {
        sync_state();
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    });

    std::cout << "[LIVE ENGINE] Starting responsive event loop (WebSocket driven)..."
              << std::endl;
    while (is_running) {
      {
        std::unique_lock<std::mutex> lock(tick_mtx);
        tick_cv.wait_for(lock, std::chrono::milliseconds(100));
      }

      if (!is_running) break;

      GlobalAlgoManager.tick(*this);
      check_kill_switch();
    }
}

void LiveExecutionEngine::sync_state() {
    Price total_balance(0);
    std::unordered_map<uint32_t, int64_t> new_positions;

    for (auto b : backends_) {
      total_balance = total_balance + b->get_balance();
      
      std::string pos_json = b->get_positions();
      try {
        auto j = nlohmann::json::parse(pos_json);
        if (j.is_array()) {
            for (const auto &p : j) {
                std::string ticker;
                if (p.contains("asset_id")) ticker = p["asset_id"];
                else if (p.contains("token_id")) ticker = p["token_id"];

                if (!ticker.empty() && p.contains("size")) {
                    int64_t qty = std::stoll(p["size"].get<std::string>());
                    new_positions[fnv1a(ticker.c_str())] += qty;
                }
            }
        } else if (j.contains("positions")) {
          for (const auto &p : j["positions"]) {
            std::string ticker;
            if (p.contains("market_ticker")) ticker = p["market_ticker"];
            else if (p.contains("ticker")) ticker = p["ticker"];

            if (!ticker.empty()) {
                int64_t qty = 0;
                if (p.contains("quantity")) qty = p["quantity"].get<int64_t>();
                else if (p.contains("size")) {
                    if (p["size"].is_string()) qty = std::stoll(p["size"].get<std::string>());
                    else qty = p["size"].get<int64_t>();
                }
                new_positions[fnv1a(ticker.c_str())] += qty;
            }
          }
        }
      } catch (...) {}
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      cached_balance = total_balance;
      cached_positions = std::move(new_positions);
      db.log_pnl_snapshot(cached_balance, get_pnl(), current_daily_pnl_raw.load());
    }
    std::cout << "[LIVE ENGINE] Synced state: Balance=" << cached_balance << " Markets=" << cached_positions.size() << std::endl;
}

// --- StreamingMarketBackend ---

void StreamingMarketBackend::update_price(MarketId market, Price yes, Price no) {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      price_cache_[market.hash] = {yes, no};
    }
    if (engine_)
      engine_->trigger_tick();
}

void StreamingMarketBackend::update_orderbook(MarketId market, const OrderBook &ob) {
    std::function<void(const OrderBook &)> cb;
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      orderbook_cache_[market.hash] = ob;
      auto it = callbacks_.find(market.hash);
      if (it != callbacks_.end()) {
        cb = it->second;
      }
    }
    if (cb)
      cb(ob);
    if (engine_)
      engine_->trigger_tick();
}

void StreamingMarketBackend::update_orderbook_incremental(MarketId market, bool is_bid, const OrderBookLevel &level) {
    std::function<void(const OrderBook &)> cb;
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto &ob = orderbook_cache_[market.hash];
      auto &side = is_bid ? ob.bids : ob.asks;

      // Find by order_id or price
      bool found = false;
      for (auto &existing : side) {
          if (!level.order_id.empty() && existing.order_id == level.order_id) {
              if (level.quantity <= 0) {
                  // Remove
                  side.erase(std::remove_if(side.begin(), side.end(), [&](const auto& l){ return l.order_id == level.order_id; }), side.end());
              } else {
                  existing.quantity = level.quantity;
                  existing.price = level.price;
              }
              found = true;
              break;
          } else if (existing.price == level.price) {
              if (level.quantity <= 0) {
                  side.erase(std::remove_if(side.begin(), side.end(), [&](const auto& l){ return l.price == level.price; }), side.end());
              } else {
                  existing.quantity = level.quantity;
              }
              found = true;
              break;
          }
      }

      if (!found && level.quantity > 0) {
          side.push_back(level);
          // Re-sort
          if (is_bid) {
              std::sort(side.begin(), side.end(), [](const auto& a, const auto& b){ return a.price > b.price; });
          } else {
              std::sort(side.begin(), side.end(), [](const auto& a, const auto& b){ return a.price < b.price; });
          }
      }

      auto it = callbacks_.find(market.hash);
      if (it != callbacks_.end()) {
        cb = it->second;
      }
      
      if (cb) cb(ob);
    }
    
    if (engine_)
      engine_->trigger_tick();
}

void StreamingMarketBackend::notify_fill(const std::string &id, int qty, Price price) {
    if (engine_) {
        engine_->add_order_fill(id, qty, price);
    }
}

void StreamingMarketBackend::notify_status(const std::string &id, OrderStatus status) {
    if (engine_) {
        engine_->update_order_status(id, status);
    }
}

// --- Dispatch Operators ---

void operator>>(const Order &o, ExecutionEngine &engine) {
  Order order_to_dispatch = o;

  if (engine.limits.dynamic_sizing_enabled) {
      order_to_dispatch.quantity = engine.calculate_dynamic_size(o);
  }

  uint64_t now =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  uint64_t latency = now - order_to_dispatch.creation_timestamp_ns;

  if (!engine.check_risk(order_to_dispatch)) {
    std::cout << "[ENGINE] Order rejected by risk engine." << std::endl;
    return;
  }

  if (order_to_dispatch.algo_type != AlgoType::None) {
    std::cout << "[ALGO] Registering " << (int)order_to_dispatch.algo_type << " for "
              << order_to_dispatch.market.ticker << std::endl;
    GlobalAlgoManager.submit(order_to_dispatch);
    return;
  }

  if (order_to_dispatch.backend) {
    std::string id;
    if (order_to_dispatch.is_spread) {
      std::cout << "[BACKEND] Dispatching spread order (" << order_to_dispatch.market.hash
                << " - " << order_to_dispatch.market2.hash << ") to " << order_to_dispatch.backend->name()
                << " (" << latency << " ns latency)" << std::endl;
      id = order_to_dispatch.backend->create_order(order_to_dispatch);
    } else {
      std::cout << "[BACKEND] Dispatching to " << order_to_dispatch.backend->name() << " ("
                << latency << " ns latency)" << std::endl;
      id = order_to_dispatch.backend->create_order(order_to_dispatch);
    }
    
    if (!id.empty() && id != "error") {
      engine.track_order(id, order_to_dispatch);
    }
  } else {
    if (order_to_dispatch.is_spread) {
      std::cout << "[ENGINE] No backend bound for spread order ("
                << order_to_dispatch.market.hash << " - " << order_to_dispatch.market2.hash
                << "). Simulated latency: " << latency << " ns." << std::endl;
    } else {
      std::cout << "[ENGINE] No backend bound. Simulated latency: " << latency
                << " ns." << std::endl;
    }
  }
}

void operator>>(std::initializer_list<Order> batch, ExecutionEngine &engine) {
  if (batch.size() == 0)
    return;

  const MarketBackend *common_backend = batch.begin()->backend;
  bool all_same = true;
  for (const auto &o : batch) {
    if (o.backend != common_backend) {
      all_same = false;
      break;
    }
  }

  if (all_same && common_backend) {
    std::cout << "[BATCH] Dispatching " << batch.size() << " orders to "
              << common_backend->name() << std::endl;
    std::vector<Order> orders(batch);
    common_backend->create_batch_orders(orders);
  } else {
    std::cout << "[BATCH] Heterogeneous batch. Dispatching individually..."
              << std::endl;
    for (const auto &o : batch) {
      o >> engine;
    }
  }
}

void operator>>(const OCOOrder &oco, ExecutionEngine &engine) {
  std::cout << "[OCO] Dispatching OCO pair..." << std::endl;
  oco.order1 >> engine;
  oco.order2 >> engine;
}

} // namespace bop
