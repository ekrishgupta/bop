#pragma once

#include "../../core/streaming_backend.hpp"
#include <iostream>

namespace bop::exchanges {

/**
 * @brief PredictIt exchange backend.
 * Uses HTTP polling as PredictIt does not have a public WebSocket for real-time data.
 */
struct PredictIt : public StreamingMarketBackend {
  PredictIt()
      : StreamingMarketBackend(std::make_unique<NullWebSocketClient>()) {
    // No WS to connect to
  }

  std::string name() const override { return "PredictIt"; }

  void sync_markets() override {
    // Fetch all markets
    std::string url = "https://www.predictit.org/api/marketdata/all/";
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        if (j.contains("markets")) {
          for (auto &m : j["markets"]) {
            std::string ticker = m["shortName"];
            int id = m["id"];
            ticker_to_id[ticker] = std::to_string(id);
            
            // Also map contract tickers if available
            if (m.contains("contracts")) {
                for (auto &c : m["contracts"]) {
                    std::string c_ticker = c["shortName"];
                    int c_id = c["id"];
                    ticker_to_id[ticker + ":" + c_ticker] = std::to_string(c_id);
                }
            }
          }
        }
      }
    } catch (const std::exception& e) {
        std::cerr << "[PredictIt] Sync Error: " << e.what() << std::endl;
    }
  }

  // --- Exchange Status ---
  std::string get_exchange_status() const override { return "active"; }
  std::string get_exchange_schedule() const override { return "24/7"; }

  // --- Market Data ---
  Price get_price_http(MarketId market, bool outcome_yes) const override {
    std::string resolved = resolve_ticker(market.ticker);
    // If it's a market ID, we might need to drill down to contracts.
    // PredictIt API for a single market: /api/marketdata/markets/[marketId]
    std::string url = "https://www.predictit.org/api/marketdata/markets/" + resolved;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        // If we resolved to a market, we take the first contract or match by name
        if (j.contains("contracts") && !j["contracts"].empty()) {
            auto& c = j["contracts"][0]; // Simple heuristic: first contract
            double last_price = c["lastTradePrice"];
            if (!outcome_yes) last_price = 1.0 - last_price;
            return Price::from_double(last_price);
        }
      }
    } catch (...) {}
    return Price::from_cents(50);
  }

  OrderBook get_orderbook_http(MarketId market) const override {
    // PredictIt doesn't provide a full order book via public API easily,
    // but it has best bid/offer in some endpoints.
    return {{{Price::from_cents(49), 100}}, {{Price::from_cents(51), 100}}};
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(1);
  }

  // WebSocket implementation (PredictIt doesn't use it, but we must implement the pure virtual)
  void send_subscription(MarketId) const override {}
  void handle_message(const std::string &) override {}

  // --- Trading ---
  // PredictIt trading is more restricted and often requires browser-like auth or specific API keys
  // for their private API. For this prototype, we'll implement placeholders.
  std::string create_order(const Order &o) const override {
    std::cout << "[PredictIt] Place Order: " << (o.is_buy ? "BUY " : "SELL ") 
              << o.quantity << " @ " << o.price.to_usd_string() << std::endl;
    return "pi_order_id_123";
  }

  bool cancel_order(const std::string &id) const override {
    std::cout << "[PredictIt] Cancel Order: " << id << std::endl;
    return true;
  }

  // --- Portfolio ---
  Price get_balance() const override {
    return Price::from_cents(10000); // Mock $100
  }

  std::string get_positions() const override {
    return "{"positions": []}";
  }
};

static PredictIt predictit;

inline MarketTarget PredictItMarket(const char *ticker) {
  return Market(ticker, predictit);
}

} // namespace bop::exchanges
