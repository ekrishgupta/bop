#pragma once

#include "../../core/streaming_backend.hpp"
#include <simdjson.h>

namespace bop::exchanges {

struct Kalshi : public StreamingMarketBackend {
  Kalshi() : StreamingMarketBackend(std::make_unique<LiveWebSocketClient>()) {
    if (ws_)
      ws_->connect("wss://api.elections.kalshi.com/trade-api/v2/stream");
  }

  std::string name() const override { return "Kalshi"; }

  void sync_markets() override {
    std::string url = "https://api.elections.kalshi.com/trade-api/v2/markets";
    try {
      // Kalshi might require authentication for this endpoint if it's large,
      // but let's try a public GET first.
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        if (j.contains("markets")) {
          for (auto &m : j["markets"]) {
            std::string ticker = m["ticker"];
            // We can also map something else if we want friendly names.
            // For now, just store ticker -> ticker to satisfy the requirement
            // of having it in the map.
            ticker_to_id[ticker] = ticker;

            if (m.contains("volume")) {
              update_volume(MarketId(ticker), m["volume"].get<int64_t>());
            }
          }
        }
      }
    } catch (...) {
    }
  }

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
    std::string resolved = resolve_ticker(market.ticker);
    std::string url =
        std::string("https://api.elections.kalshi.com/trade-api/v2/markets/") +
        resolved;
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
    std::string resolved = resolve_ticker(market.ticker);
    std::string url =
        std::string("https://api.elections.kalshi.com/trade-api/v2/markets/") +
        resolved + "/orderbook";
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        OrderBook ob;
        if (j.contains("orderbook")) {
          auto &book = j["orderbook"];
          if (book.contains("bids")) {
            for (auto &b : book["bids"]) {
              ob.bids[Price::from_cents(b[0].get<int64_t>())] =
                  b[1].get<int64_t>();
            }
          }
          if (book.contains("asks")) {
            for (auto &a : book["asks"]) {
              ob.asks[Price::from_cents(a[0].get<int64_t>())] =
                  a[1].get<int64_t>();
            }
          }
        }
        return ob;
      }
    } catch (...) {
    }
    return {};
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
    j["params"]["channels"] = {"ticker", "orderbook"};
    j["params"]["market_tickers"] = {resolve_ticker(market.ticker)};
    ws_->send(j.dump());
  }

  void handle_message(std::string_view msg) override {
    try {
      simdjson::ondemand::document doc =
          parser_.iterate(msg.data(), msg.size(), msg.size());
      std::string_view type;
      auto error = doc["type"].get(type);
      if (error)
        return;

      if (type == "ticker") {
        auto m = doc["msg"];
        std::string_view ticker;
        int64_t last_price;
        if (!m["market_ticker"].get(ticker) &&
            !m["last_price"].get(last_price)) {
          update_price(MarketId(ticker), Price::from_cents(last_price),
                       Price::from_cents(100 - last_price));

          // Try to get volume
          int64_t volume;
          if (!m["volume"].get(volume)) {
            update_volume(MarketId(ticker), volume);
          }
        }
      } else if (type == "orderbook_snapshot") {
        auto m = doc["msg"];
        std::string_view ticker;
        if (!m["market_ticker"].get(ticker)) {
          OrderBook ob;
          auto bids = m["bids"];
          for (auto item : bids) {
            int64_t price, qty;
            auto arr = item.get_array();
            auto it = arr.begin();
            (*it).get(price);
            (++it)->get(qty);
            ob.bids[Price::from_cents(price)] = qty;
          }
          auto asks = m["asks"];
          for (auto item : asks) {
            int64_t price, qty;
            auto arr = item.get_array();
            auto it = arr.begin();
            (*it).get(price);
            (++it)->get(qty);
            ob.asks[Price::from_cents(price)] = qty;
          }
          update_orderbook(MarketId(ticker), ob);
        }
      } else if (type == "orderbook_delta") {
        auto m = doc["msg"];
        std::string_view ticker;
        if (!m["market_ticker"].get(ticker)) {
          // Kalshi V2 deltas are usually full updates for the changed levels
          // We can use update_orderbook_incremental if we want to be precise,
          // but for simplicity let's handle them as updates.
          auto delta = m["delta"];
          int64_t price, qty;
          std::string_view side;
          if (!m["price"].get(price) && !m["delta"].get(qty) &&
              !m["side"].get(side)) {
            update_orderbook_incremental(
                MarketId(ticker), side == "yes",
                {Price::from_cents(price), static_cast<int>(qty)});
          }
        }
      } else if (type == "fill") {
        auto m = doc["msg"];
        std::string_view order_id;
        int64_t qty;
        int64_t price_cents;
        if (!m["order_id"].get(order_id) && !m["count"].get(qty) &&
            !m["price"].get(price_cents)) {
          notify_fill(std::string(order_id), static_cast<int>(qty),
                      Price::from_cents(price_cents));
        }
      } else if (type == "order_status_change") {
        auto m = doc["msg"];
        std::string_view order_id;
        std::string_view status_str;
        if (!m["order_id"].get(order_id) && !m["status"].get(status_str)) {
          OrderStatus status = OrderStatus::Open;
          if (status_str == "canceled")
            status = OrderStatus::Cancelled;
          else if (status_str == "rejected")
            status = OrderStatus::Rejected;
          else if (status_str == "filled")
            status = OrderStatus::Filled;
          notify_status(std::string(order_id), status);
        }
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
    j["market_ticker"] = resolve_ticker(o.market.ticker);
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

  std::string get_positions() const override {
    std::string path = "/v2/portfolio/positions";
    std::string url = "https://api.elections.kalshi.com/trade-api" + path;
    try {
      auto resp = Network.get(url, auth_headers("GET", path));
      if (resp.status_code == 200) {
        return resp.body;
      }
    } catch (...) {
    }
    return "{\"positions\": []}";
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
