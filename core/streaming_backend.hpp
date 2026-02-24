#pragma once

#include "market_base.hpp"
#include "websocket.hpp"
#include "simdjson.h"
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace bop {

struct ExecutionEngine;

/**
 * @brief A backend that uses WebSockets to maintain a live view of the market.
 * This reduces latency by avoiding HTTP polling and providing immediate access
 * to the latest data.
 */
class StreamingMarketBackend : public MarketBackend {
public:
  inline explicit StreamingMarketBackend(std::unique_ptr<WebSocketClient> ws)
      : ws_(std::move(ws)) {
    if (ws_) {
      ws_->on_message(
          [this](const std::string &msg) { this->handle_message(msg); });

      // Handle automatic re-subscription on connection/reconnection
      ws_->on_open([this]() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        for (const auto &[hash, market] : active_subscriptions_) {
          send_subscription(market);
        }
      });
    }
  }

  void set_engine(ExecutionEngine* engine) { engine_ = engine; }

  // Market Data (Live) - Now returns cached data with fallback
  Price get_price(MarketId market, bool outcome_yes) const override {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = price_cache_.find(market.hash);
      if (it != price_cache_.end()) {
        return outcome_yes ? it->second.yes_price : it->second.no_price;
      }
    }
    return get_price_http(market, outcome_yes);
  }

  // Virtual fallbacks to be implemented by child or remain as defaults
  virtual Price get_price_http(MarketId market, bool outcome_yes) const {
    return Price(0);
  }

  OrderBook get_orderbook(MarketId market) const override {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = orderbook_cache_.find(market.hash);
      if (it != orderbook_cache_.end()) {
        return it->second;
      }
    }
    return get_orderbook_http(market);
  }

  virtual OrderBook get_orderbook_http(MarketId market) const { return {}; }

  // WebSocket implementation
  void ws_subscribe_orderbook(
      MarketId market,
      std::function<void(const OrderBook &)> callback) const override {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    callbacks_[market.hash] = callback;
    active_subscriptions_[market.hash] = market; // Track for re-subscription
    if (ws_ && ws_->is_connected()) {
      send_subscription(market);
    }
  }

  virtual void send_subscription(MarketId market) const = 0;

protected:
  virtual void handle_message(const std::string &msg) = 0;

  void update_price(MarketId market, Price yes, Price no) {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      price_cache_[market.hash] = {yes, no};
    }
    if (engine_)
      engine_->trigger_tick();
  }

  void update_orderbook(MarketId market, const OrderBook &ob) {
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

  void notify_fill(const std::string &id, int qty, Price price);
  void notify_status(const std::string &id, OrderStatus status);

  mutable std::mutex cache_mutex_;
  std::unique_ptr<WebSocketClient> ws_;
  ExecutionEngine* engine_ = nullptr;
  mutable simdjson::ondemand::parser parser_;

  struct PricePair {
    Price yes_price;
    Price no_price;
  };
  mutable std::map<uint32_t, PricePair> price_cache_;
  mutable std::map<uint32_t, OrderBook> orderbook_cache_;
  mutable std::map<uint32_t, std::function<void(const OrderBook &)>> callbacks_;
  mutable std::map<uint32_t, MarketId> active_subscriptions_;
};

} // namespace bop
