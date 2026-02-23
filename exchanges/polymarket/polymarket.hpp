#pragma once

#include "../../core/market_base.hpp"

namespace bop::exchanges {

struct Polymarket : public MarketBackend {
  std::string name() const override { return "Polymarket"; }

  // --- Exchange Status & Metadata ---
  int64_t clob_get_server_time() const override {
    return 1709400000; // Mock CLOB timestamp
  }

  // --- Market Data (Live) ---
  Price get_price(MarketId market, bool outcome_yes) const override {
    std::string url =
        std::string("https://clob.polymarket.com/last-trade-price?token_id=") +
        market.ticker;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        // Polymarket returns { "price": "0.60", "side": "buy" }
        double price_val = std::stod(j["price"].get<std::string>());
        if (!outcome_yes)
          price_val = 1.0 - price_val;
        return Price::from_double(price_val);
      }
    } catch (...) {
    }
    return Price::from_cents(60); // Mock fallback
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(5); // Mock spread
  }

  OrderBook get_orderbook(MarketId) const override {
    return {{{Price::from_cents(59), 200}},
            {{Price::from_cents(61), 250}}}; // Mock L2 book
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, Price::from_cents(60), Price::from_cents(62),
             Price::from_cents(58), Price::from_cents(61), 5000}}; // Mock OHLCV
  }

  // --- Gamma Market Discovery ---
  std::string gamma_get_event(const std::string &id) const override {
    return "{\"event\": \"id_" + id + "\"}";
  }

  std::string gamma_get_market(const std::string &id) const override {
    return "{\"market\": \"id_" + id + "\"}";
  }

  // --- CLOB Specifics ---
  Price clob_get_midpoint(MarketId market) const override {
    return Price::from_cents(60);
  }
  Price clob_get_spread(MarketId market) const override {
    return Price::from_cents(2);
  }
  Price clob_get_last_trade_price(MarketId market) const override {
    return Price::from_cents(61);
  }
  double clob_get_fee_rate(MarketId market) const override { return 0.005; }
  Price clob_get_tick_size(MarketId market) const override {
    return Price::from_cents(1);
  }

  // --- Historical ---
  std::vector<Candlestick>
  get_historical_candlesticks(MarketId market) const override {
    return {{0, Price::from_cents(50), Price::from_cents(60),
             Price::from_cents(48), Price::from_cents(59),
             10000}}; // Mock clob_get_prices_history
  }

  // --- Trading ---
  std::string create_order(const Order &) const override {
    return "polymarket_clob_uuid";
  }
  bool cancel_order(const std::string &) const override { return true; }

  // --- Portfolio (Mock via CLOB) ---
  Price get_balance() const override { return Price::from_usd(500000); }
  std::string get_positions() const override { return "{\"positions\": []}"; }

  // --- WebSocket Streaming ---
  void ws_subscribe_orderbook(
      MarketId market,
      std::function<void(const OrderBook &)> callback) const override {
    std::cout << "[POLYMARKET] WS Subscribe Orderbook: " << market.hash
              << std::endl;
  }

  void ws_subscribe_trades(
      MarketId market,
      std::function<void(Price, int64_t)> callback) const override {
    std::cout << "[POLYMARKET] WS Subscribe Trades: " << market.hash
              << std::endl;
  }

  void ws_unsubscribe(MarketId market) const override {
    std::cout << "[POLYMARKET] WS Unsubscribe: " << market.hash << std::endl;
  }

  // Authentication
  std::string sign_request(const std::string &method, const std::string &path,
                           const std::string &body = "") const {
    auto now = std::chrono::system_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();
    std::string timestamp = std::to_string(ms);
    return auth::PolySigner::sign(credentials.secret_key, timestamp, method,
                                  path, body);
  }
};

static const Polymarket polymarket;

// Helper for Polymarket-specific targets
constexpr MarketTarget PolyMarket(const char *id) {
  return Market(id, polymarket);
}

} // namespace bop::exchanges
