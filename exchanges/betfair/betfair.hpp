#pragma once

#include "../../core/streaming_backend.hpp"
#include <iostream>

namespace bop::exchanges {

/**
 * @brief Betfair exchange backend.
 * Betfair uses decimal odds. Conversion: Odds = 1.0 / Price.
 */
struct Betfair : public StreamingMarketBackend {
  Betfair()
      : StreamingMarketBackend(std::make_unique<NullWebSocketClient>()) {
    // Betfair has a streaming API, but we'll use polling for the initial prototype.
  }

  std::string name() const override { return "Betfair"; }

  // Betfair requires a session token which is obtained via login
  std::string session_token;

  void load_markets() override {
    if (credentials.api_key.empty()) return;

    std::string url = "https://api.betfair.com/exchange/betting/json-rpc/v1";
    json j;
    j["jsonrpc"] = "2.0";
    j["method"] = "SportsAPING/v1.0/listMarketCatalogue";
    j["params"]["filter"]["eventTypeIds"] = {"7"}; // Horse Racing or use "1" for Soccer
    j["params"]["maxResults"] = 100;
    j["params"]["marketProjection"] = {"RUNNER_DESCRIPTION"};
    j["id"] = 1;

    try {
      auto resp = Network.post(url, j.dump(), auth_headers());
      if (resp.status_code == 200) {
        auto res_j = resp.json_body();
        if (res_j.contains("result")) {
            for (auto &m : res_j["result"]) {
                std::string name = m["marketName"];
                std::string id = m["marketId"];
                ticker_to_id[name] = id;
            }
        }
      }
    } catch (...) {}
  }

  std::map<std::string, std::string> auth_headers() const {
    return {
        {"X-Application", credentials.api_key},
        {"X-Authentication", session_token},
        {"Content-Type", "application/json"},
        {"Accept", "application/json"}
    };
  }

  // --- Market Data ---
  Price get_price_http(MarketId market, bool outcome_yes) const override {
    std::string resolved = resolve_ticker(market.ticker);
    std::string url = "https://api.betfair.com/exchange/betting/json-rpc/v1";
    
    json j;
    j["jsonrpc"] = "2.0";
    j["method"] = "SportsAPING/v1.0/listMarketBook";
    j["params"]["marketIds"] = {resolved};
    j["params"]["priceProjection"]["priceData"] = {"EX_BEST_OFFERS"};
    j["id"] = 1;

    try {
      auto resp = Network.post(url, j.dump(), auth_headers());
      if (resp.status_code == 200) {
        auto res_j = resp.json_body();
        if (res_j.contains("result") && !res_j["result"].empty()) {
            auto& book = res_j["result"][0];
            if (book.contains("runners") && !book["runners"].empty()) {
                auto& runner = book["runners"][0]; // First runner
                if (runner.contains("ex") && runner["ex"].contains("availableToBack") && 
                    !runner["ex"]["availableToBack"].empty()) {
                    double odds = runner["ex"]["availableToBack"][0]["price"];
                    double prob = 1.0 / odds;
                    if (!outcome_yes) prob = 1.0 - prob;
                    return Price::from_double(prob);
                }
            }
        }
      }
    } catch (...) {}
    return Price::from_cents(50);
  }

  OrderBook get_orderbook_http(MarketId market) const override {
    return {{{Price::from_cents(48), 500}}, {{Price::from_cents(52), 500}}};
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(1);
  }

  void send_subscription(MarketId) const override {}
  void handle_message(const std::string &) override {}

  // --- Trading ---
  std::string create_order(const Order &o) const override {
    // Betfair uses PlaceOrders
    std::cout << "[Betfair] Place Order: " << (o.is_buy ? "BACK " : "LAY ") 
              << o.quantity << " @ " << (1.0 / o.price.to_double()) << " odds" << std::endl;
    return "bf_order_id_456";
  }

  bool cancel_order(const std::string &id) const override {
    return true;
  }

  Price get_balance() const override {
    return Price::from_cents(25000); // Mock $250
  }
};

static Betfair betfair;

inline MarketTarget BetfairMarket(const char *name) {
  return Market(name, betfair);
}

} // namespace bop::exchanges
