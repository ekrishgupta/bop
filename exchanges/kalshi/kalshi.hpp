#pragma once

#include "../../core/market_base.hpp"

namespace bop::exchanges {

struct Kalshi : public MarketBackend {
  std::string name() const override { return "Kalshi"; }

  // Historical Cutoffs (RFC3339 formatted as per docs)
  struct Cutoffs {
    std::string market_settled_ts;
    std::string trades_created_ts;
    std::string orders_updated_ts;
  } cutoffs;

  // --- Exchange & Status ---
  std::string get_exchange_status() const override { return "active"; }
  std::string get_exchange_schedule() const override { return "24/7"; }

  // --- Market Data ---
  Price get_price(MarketId market, bool outcome_yes) const override {
    std::string url =
        std::string("https://api.elections.kalshi.com/trade-api/v2/markets/") +
        market.ticker;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        // Kalshi V2 returns { "market": { "last_price": 50, ... } }
        int64_t cents = j["market"]["last_price"].get<int64_t>();
        if (!outcome_yes)
          cents = 100 - cents;
        return Price::from_cents(cents);
      }
    } catch (...) {
      // Fallback or error handling
    }
    return Price::from_cents(50); // Mock fallback
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(2); // Mock: spread in cents
  }

  OrderBook get_orderbook(MarketId) const override {
    return {{{Price::from_cents(50), 100}},
            {{Price::from_cents(51), 100}}}; // Mock L2
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, Price::from_cents(50), Price::from_cents(55),
             Price::from_cents(45), Price::from_cents(52), 1000}}; // Mock OHLCV
  }

  // --- Historical ---
  std::string get_historical_cutoff() const override {
    return "2023-11-07T05:31:56Z";
  }

  // --- Trading ---
  std::string create_order(const Order &o) const override {
    std::string path = "/v2/portfolio/orders";
    std::string url = "https://api.elections.kalshi.com/trade-api" + path;

    json j;
    j["action"] = o.is_buy ? "buy" : "sell";
    j["amount"] = o.quantity;
    j["market_ticker"] = o.market.ticker;
    j["side"] = o.outcome_yes ? "yes" : "no";
    j["type"] = (o.price.raw == 0) ? "market" : "limit";

    if (o.price.raw != 0) {
      if (o.outcome_yes)
        j["yes_price"] = o.price.to_cents();
      else
        j["no_price"] = o.price.to_cents();
    }

    std::string body = j.dump();
    try {
      auto resp = Network.post(url, body, auth_headers("POST", path, body));
      if (resp.status_code == 201 || resp.status_code == 200) {
        auto res_j = resp.json_body();
        return res_j["order"]["order_id"].get<std::string>();
      } else {
        std::cerr << "[KALSHI] Order Error: " << resp.body << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[KALSHI] Order Exception: " << e.what() << std::endl;
    }
    return "error";
  }
  bool cancel_order(const std::string &) const override { return true; }

  // --- Portfolio ---
  Price get_balance() const override {
    std::string path = "/v2/portfolio/balance";
    std::string url = "https://api.elections.kalshi.com/trade-api" + path;
    try {
      auto resp = Network.get(url, auth_headers("GET", path));
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        // Kalshi returns balance in cents
        return Price::from_cents(j["balance"].get<int64_t>());
      }
    } catch (...) {
    }
    return Price(0);
  }

  PortfolioSummary get_portfolio_summary() const override {
    return {get_balance(), Price(0), Price(0), Price(0)};
  }

  // Cent-pricing validator
  static bool is_valid_price(int64_t cents) {
    return cents >= 1 && cents <= 99;
  }

  // --- WebSocket Streaming ---
  void ws_subscribe_orderbook(
      MarketId market,
      std::function<void(const OrderBook &)> callback) const override {
    std::cout << "[KALSHI] WS Subscribe Orderbook: " << market.hash
              << std::endl;
  }

  void ws_subscribe_trades(
      MarketId market,
      std::function<void(Price, int64_t)> callback) const override {
    std::cout << "[KALSHI] WS Subscribe Trades: " << market.hash << std::endl;
  }

  void ws_unsubscribe(MarketId market) const override {
    std::cout << "[KALSHI] WS Unsubscribe: " << market.hash << std::endl;
  }

  // Authentication Helpers
  std::map<std::string, std::string>
  auth_headers(const std::string &method, const std::string &path,
               const std::string &body = "") const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count();
    std::string timestamp = std::to_string(ms);
    std::string signature = auth::KalshiSigner::sign(
        credentials.secret_key, timestamp, method, path, body);

    return {{"KALSHI-ACCESS-KEY", credentials.api_key},
            {"KALSHI-ACCESS-SIGNATURE", signature},
            {"KALSHI-ACCESS-TIMESTAMP", timestamp}};
  }

  std::string sign_request(const std::string &method, const std::string &path,
                           const std::string &body = "") const {
    auto headers = auth_headers(method, path, body);
    return headers.at("KALSHI-ACCESS-SIGNATURE");
  }
};

static Kalshi kalshi;

// Helper for Kalshi-specific tickers
MarketTarget KalshiTicker(const char *ticker) { return Market(ticker, kalshi); }

} // namespace bop::exchanges
