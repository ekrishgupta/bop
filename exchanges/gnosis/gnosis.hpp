#pragma once

#include "../../core/streaming_backend.hpp"
#include <iostream>

namespace bop::exchanges {

/**
 * @brief Gnosis decentralized exchange backend.
 * Typically interacts with Conditional Tokens Framework on Gnosis Chain.
 */
struct Gnosis : public StreamingMarketBackend {
  Gnosis()
      : StreamingMarketBackend(std::make_unique<NullWebSocketClient>()) {
    // On-chain data usually fetched via RPC or Indexers (The Graph)
  }

  std::string name() const override { return "Gnosis"; }

  void sync_markets() override {
    // In a real implementation, this would query a subgraph
    ticker_to_id["GNOSIS_EXAMPLE"] = "0x123...456";
  }

  // --- Market Data ---
  Price get_price_http(MarketId market, bool outcome_yes) const override {
    // Query AMM (like Omen or Azuro) for price
    return Price::from_cents(55);
  }

  OrderBook get_orderbook_http(MarketId market) const override {
    // AMMs don't have traditional orderbooks, but we can simulate it
    return {{{Price::from_cents(54), 1000}}, {{Price::from_cents(56), 1000}}};
  }

  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(2);
  }

  void send_subscription(MarketId) const override {}
  void handle_message(const std::string &) override {}

  // --- Trading ---
  std::string create_order(const Order &o) const override {
    std::cout << "[Gnosis] Send Transaction: " << (o.is_buy ? "BUY " : "SELL ") 
              << o.quantity << " tokens" << std::endl;
    return "0x_tx_hash_789";
  }

  bool cancel_order(const std::string &) const override {
    return false; // Transactions can't be "cancelled" easily once mined
  }

  Price get_balance() const override {
    return Price::from_cents(50000); // Mock $500 in xDAI
  }
};

static Gnosis gnosis;

inline MarketTarget GnosisMarket(const char *id) {
  return Market(id, gnosis);
}

} // namespace bop::exchanges
