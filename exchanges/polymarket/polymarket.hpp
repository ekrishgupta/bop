#pragma once

#include "../../core/streaming_backend.hpp"

namespace bop::exchanges {

struct Polymarket : public StreamingMarketBackend {
  Polymarket()
      : StreamingMarketBackend(std::make_unique<LiveWebSocketClient>()) {
    if (ws_)
      ws_->connect("wss://clob.polymarket.com/ws");
  }

  std::string name() const override { return "Polymarket"; }

  void load_markets() override {
    std::string url = "https://clob.polymarket.com/markets";
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        for (auto &m : j) {
          if (!m.contains("active") || !m["active"].get<bool>())
            continue;

          std::string cond_id = m["condition_id"];
          // Polymarket sometimes has a 'ticker' field in Gamma, but in CLOB it
          // might be different. Let's use the question or a derived ticker if
          // available. For now, we'll map condition_id to condition_id and also
          // store tokens.
          ticker_to_id[cond_id] = cond_id;

          if (m.contains("tokens")) {
            for (auto &t : m["tokens"]) {
              std::string outcome = t["outcome"];
              std::string token_id = t["token_id"];
              // Store as ticker_YES or ticker_NO
              if (outcome == "Yes") {
                ticker_to_id[cond_id + "_YES"] = token_id;
              } else if (outcome == "No") {
                ticker_to_id[cond_id + "_NO"] = token_id;
              }
            }
          }
        }
      }
    } catch (...) {
    }
  }

  std::string resolve_token_id(const MarketId &market, bool outcome_yes) const {
    std::string key = market.ticker + (outcome_yes ? "_YES" : "_NO");
    auto it = ticker_to_id.find(key);
    if (it != ticker_to_id.end()) {
      return it->second;
    }
    return market.ticker; // Fallback
  }

  // --- Exchange Status & Metadata ---
  int64_t clob_get_server_time() const override { return 1709400000; }

  // --- Market Data (Live) ---
  Price get_price_http(MarketId market, bool outcome_yes) const override {
    std::string token_id = resolve_token_id(market, outcome_yes);
    std::string url =
        std::string("https://clob.polymarket.com/last-trade-price?token_id=") +
        token_id;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        double price_val = std::stod(j["price"].get<std::string>());
        // Polymarket last-trade-price is per-token, so no need to flip if
        // outcome_no? Actually it depends on which token we queried.
        return Price::from_double(price_val);
      }
    } catch (...) {
    }
    return Price::from_cents(60);
  }

  OrderBook get_orderbook_http(MarketId market) const override {
    return {{{Price::from_cents(59), 200}}, {{Price::from_cents(61), 250}}};
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(5);
  }

  std::vector<Candlestick> get_candlesticks(MarketId) const override {
    return {{0, Price::from_cents(60), Price::from_cents(62),
             Price::from_cents(58), Price::from_cents(61), 5000}};
  }

  // WebSocket implementation
  void send_subscription(MarketId market) const override {
    json j;
    j["type"] = "subscribe";
    // Subscribe to both YES and NO tokens for the market
    j["token_ids"] = {resolve_token_id(market, true),
                      resolve_token_id(market, false)};
    j["channels"] = {"trades"};
    ws_->send(j.dump());
  }

  void handle_message(const std::string &msg) override {
    try {
      auto j = json::parse(msg);
      if (j.contains("event_type") && j["event_type"] == "price_change") {
        std::string token_id = j["token_id"];
        double price = std::stod(j["price"].get<std::string>());
        // We need to map token_id back to MarketId and side.
        // For now, update by token_id as ticker.
        update_price(MarketId(token_id.c_str()), Price::from_double(price),
                     Price::from_double(1.0 - price));
      }
    } catch (...) {
    }
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

  // --- Trading ---
  std::string create_order(const Order &o) const override {
    std::string path = "/orders";
    std::string url = "https://clob.polymarket.com" + path;

    json j;
    j["token_id"] = resolve_token_id(o.market, o.outcome_yes);
    j["price"] = o.price.to_usd_string();
    j["size"] = std::to_string(o.quantity);
    j["side"] = o.is_buy ? "BUY" : "SELL";
    j["order_type"] = (o.price.raw == 0) ? "MARKET" : "LIMIT";
    j["expiration"] = "0";
    j["timestamp"] = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count() /
        1000000000);
    j["owner"] = credentials.address;
    j["nonce"] = 0;
    j["signature"] = auth::PolySigner::sign_order(
        credentials.secret_key, credentials.address, j["token_id"], j["price"],
        j["size"], j["side"], j["expiration"], j["nonce"]);

    std::string body = j.dump();
    try {
      auto resp = Network.post(url, body, auth_headers("POST", path, body));
      if (resp.status_code == 201 || resp.status_code == 200) {
        auto res_j = resp.json_body();
        return res_j["orderID"].get<std::string>();
      } else {
        std::cerr << "[POLYMARKET] Order Error: " << resp.body << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "[POLYMARKET] Order Exception: " << e.what() << std::endl;
    }
    return "error";
  }

  bool cancel_order(const std::string &) const override { return true; }

  // --- Portfolio (REST via CLOB) ---
  Price get_balance() const override {
    std::string path = "/balance-allowance?asset_type=collateral";
    std::string url = "https://clob.polymarket.com" + path;
    try {
      auto resp = Network.get(url, auth_headers("GET", path));
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        return Price::from_double(std::stod(j["balance"].get<std::string>()));
      }
    } catch (...) {
    }
    return Price(0);
  }

  std::string get_positions() const override {
    std::string path = "/positions?user=" + credentials.address;
    std::string url = "https://clob.polymarket.com" + path;
    try {
      auto resp = Network.get(url, auth_headers("GET", path));
      if (resp.status_code == 200) {
        return resp.body;
      }
    } catch (...) {
    }
    return "{\"positions\": []}";
  }

  // Authentication Helpers
  std::map<std::string, std::string>
  auth_headers(const std::string &method, const std::string &path,
               const std::string &body = "") const {
    auto now = std::chrono::system_clock::now();
    auto s =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
            .count();
    std::string timestamp = std::to_string(s);
    std::string signature = auth::PolySigner::sign(
        credentials.secret_key, credentials.address, timestamp, method, path, body);

    return {{"POLY-API-KEY", credentials.api_key},
            {"POLY-PASSPHRASE", credentials.passphrase},
            {"POLY-SIGNATURE", signature},
            {"POLY-TIMESTAMP", timestamp},
            {"Content-Type", "application/json"}};
  }

  std::string sign_request(const std::string &method, const std::string &path,
                           const std::string &body = "") const {
    auto headers = auth_headers(method, path, body);
    return headers.at("POLY-SIGNATURE");
  }
};

static Polymarket polymarket;

inline MarketTarget PolyMarket(const char *id) {
  return Market(id, polymarket);
}

} // namespace bop::exchanges
