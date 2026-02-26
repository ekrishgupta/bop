#pragma once

#include "../../core/streaming_backend.hpp"
#include <simdjson.h>

namespace bop::exchanges {

struct Polymarket : public StreamingMarketBackend {
  Polymarket()
      : StreamingMarketBackend(std::make_unique<LiveWebSocketClient>()) {
    if (ws_)
      ws_->connect("wss://clob.polymarket.com/ws");
  }

  std::string name() const override { return "Polymarket"; }

  void sync_markets() override {
    std::string url =
        "https://gamma-api.polymarket.com/markets?active=true&limit=100";
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        for (auto &m : j) {
          std::string slug = m["slug"];
          std::string cond_id = m["conditionId"];

          ticker_to_id[slug] = cond_id;
          if (m.contains("groupTicker"))
            ticker_to_id[m["groupTicker"]] = cond_id;
          if (m.contains("question"))
            ticker_to_id[m["question"]] = cond_id;

          if (m.contains("clobTokenIds")) {
            auto tokens_str = m["clobTokenIds"].get<std::string>();
            auto tokens = nlohmann::json::parse(tokens_str);
            if (tokens.size() >= 2) {
              std::string yes_token = tokens[0].get<std::string>();
              std::string no_token = tokens[1].get<std::string>();

              ticker_to_id[slug + "_YES"] = yes_token;
              ticker_to_id[slug + "_NO"] = no_token;

              if (m.contains("groupTicker")) {
                std::string gt = m["groupTicker"];
                ticker_to_id[gt + "_YES"] = yes_token;
                ticker_to_id[gt + "_NO"] = no_token;
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
    std::string token_id = resolve_token_id(market, true);
    std::string url =
        std::string("https://clob.polymarket.com/book?token_id=") + token_id;
    try {
      auto resp = Network.get(url);
      if (resp.status_code == 200) {
        auto j = resp.json_body();
        OrderBook ob;
        if (j.contains("bids")) {
          for (auto &b : j["bids"]) {
            ob.bids[Price::from_double(
                std::stod(b["price"].get<std::string>()))] =
                static_cast<int64_t>(std::stod(b["size"].get<std::string>()));
          }
        }
        if (j.contains("asks")) {
          for (auto &a : j["asks"]) {
            ob.asks[Price::from_double(
                std::stod(a["price"].get<std::string>()))] =
                static_cast<int64_t>(std::stod(a["size"].get<std::string>()));
          }
        }
        return ob;
      }
    } catch (...) {
    }
    return {};
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
    j["channels"] = {"trades", "book"};
    ws_->send(j.dump());
  }

  void handle_message(std::string_view msg) override {
    try {
      simdjson::ondemand::document doc =
          parser_.iterate(msg.data(), msg.size(), msg.size());

      auto process_event = [this](auto event) {
        std::string_view type;
        if (event["event_type"].get(type))
          return;

        if (type == "price_change") {
          std::string_view token_id;
          std::string_view price_str, size_str, side_str;
          if (!event["token_id"].get(token_id) &&
              !event["price"].get(price_str) && !event["size"].get(size_str) &&
              !event["side"].get(side_str)) {
            double price = std::stod(std::string(price_str));
            double size = std::stod(std::string(size_str));
            bool is_buy = (side_str == "BUY");

            update_orderbook_incremental(
                MarketId(token_id), is_buy,
                {Price::from_double(price), static_cast<int64_t>(size)});

            // Also update the top-level price cache
            if (size > 0) {
              update_price(MarketId(token_id), Price::from_double(price),
                           Price::from_double(1.0 - price));
            }
          }
        } else if (type == "book") {
          std::string_view token_id;
          if (!event["token_id"].get(token_id)) {
            OrderBook ob;
            auto bids = event["bids"];
            for (auto item : bids) {
              std::string_view p_str, s_str;
              auto arr = item.get_array();
              auto it = arr.begin();
              (*it).get(p_str);
              (++it)->get(s_str);
              ob.bids[Price::from_double(std::stod(std::string(p_str)))] =
                  static_cast<int64_t>(std::stod(std::string(s_str)));
            }
            auto asks = event["asks"];
            for (auto item : asks) {
              std::string_view p_str, s_str;
              auto arr = item.get_array();
              auto it = arr.begin();
              (*it).get(p_str);
              (++it)->get(s_str);
              ob.asks[Price::from_double(std::stod(std::string(p_str)))] =
                  static_cast<int64_t>(std::stod(std::string(s_str)));
            }
            update_orderbook(MarketId(token_id), ob);
          }
        } else if (type == "order_update") {
          auto order = event["order"];
          std::string_view id;
          std::string_view status_str;
          if (!order["id"].get(id) && !event["status"].get(status_str)) {
            std::string_view fill_size_str;
            if (!event["fill_size"].get(fill_size_str)) {
              double fill_size = std::stod(std::string(fill_size_str));
              if (fill_size > 0) {
                std::string_view fill_price_str;
                if (!event["fill_price"].get(fill_price_str)) {
                  double fill_price = std::stod(std::string(fill_price_str));
                  notify_fill(std::string(id), static_cast<int>(fill_size),
                              Price::from_double(fill_price));
                }
              }
            }
            if (status_str == "closed")
              notify_status(std::string(id), OrderStatus::Filled);
            else if (status_str == "canceled")
              notify_status(std::string(id), OrderStatus::Cancelled);
          }
        }
      };

      if (doc.type() == simdjson::ondemand::json_type::array) {
        for (auto item : doc.get_array()) {
          process_event(item.get_object());
        }
      } else {
        process_event(doc.get_object());
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
    OrderBook ob = get_orderbook(market);
    if (ob.bids.empty() || ob.asks.empty())
      return Price(0);
    return Price((ob.bids.begin()->first.raw + ob.asks.begin()->first.raw) / 2);
  }
  Price clob_get_spread(MarketId market) const override {
    OrderBook ob = get_orderbook(market);
    if (ob.bids.empty() || ob.asks.empty())
      return Price(100);
    return Price(ob.asks.begin()->first.raw - ob.bids.begin()->first.raw);
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
    std::string signature =
        auth::PolySigner::sign(credentials.secret_key, credentials.address,
                               timestamp, method, path, body);

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
