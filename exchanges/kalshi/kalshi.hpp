#pragma once

#include "../../core/streaming_backend.hpp"

namespace bop::exchanges {

struct Kalshi : public StreamingMarketBackend {
  Kalshi()
      : StreamingMarketBackend(std::make_unique<ProductionWebSocketClient>()) {
    if (ws_)
      ws_->connect("wss://api.elections.kalshi.com/trade-api/v2/stream");
  }

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
  Price get_price_http(MarketId market, bool outcome_yes) const override {
    std::string url =
        std::string("https://api.elections.kalshi.com/trade-api/v2/markets/") +
        market.ticker;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        int64_t cents = j["market"]["last_price"].get<int64_t>();
        if (!outcome_yes)
          cents = 100 - cents;
        return Price::from_cents(cents);
      }
    } catch (...) {
    }
    return Price::from_cents(50);
  }

  OrderBook get_orderbook_http(MarketId market) const override {
    return {{{Price::from_cents(50), 100}}, {{Price::from_cents(51), 100}}};
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(2);
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, Price::from_cents(50), Price::from_cents(55),
             Price::from_cents(45), Price::from_cents(52), 1000}};
  }

  // WebSocket implementation
  void send_subscription(MarketId market) const override {
    json j;
    j["id"] = 1;
    j["cmd"] = "subscribe";
    j["params"]["channels"] = {"ticker"};
    j["params"]["market_tickers"] = {market.ticker};
    ws_->send(j.dump());
  }

  void handle_message(const std::string &msg) override {
    try {
      auto j = json::parse(msg);
      if (j.contains("type") && j["type"] == "ticker") {
        std::string ticker = j["msg"]["market_ticker"];
        int64_t last_price = j["msg"]["last_price"];
        update_price(MarketId(ticker.c_str()), Price::from_cents(last_price),
                     Price::from_cents(100 - last_price));
      }
    } catch (...) {
    }
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
            {"KALSHI-ACCESS-TIMESTAMP", timestamp},
            {"Content-Type", "application/json"}};
  }

  std::string sign_request(const std::string &method, const std::string &path,
                           const std::string &body = "") const {
    auto headers = auth_headers(method, path, body);
    return headers.at("KALSHI-ACCESS-SIGNATURE");
  }
};

static Kalshi kalshi;

inline MarketTarget KalshiTicker(const char *ticker) {
  return Market(ticker, kalshi);
}

} // namespace bop::exchanges
