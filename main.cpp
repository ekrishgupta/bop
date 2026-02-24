#include "bop.hpp"
#include "exchanges/kalshi/kalshi.hpp"
#include "exchanges/polymarket/polymarket.hpp"
#include <iostream>
#include <thread>

struct MockEngine : public ExecutionEngine {
  int64_t get_position(MarketId) const override { return 100; }
  Price get_balance() const override { return Price::from_usd(10000); }
  Price get_exposure() const override { return Price::from_usd(500); }
  Price get_price(MarketId, bool) const override {
    return Price::from_cents(52);
  }
  Price get_depth(MarketId, bool) const override {
    return Price::from_cents(50);
  }
  int64_t get_volume(MarketId) const override {
    static int64_t vol = 1000;
    vol += 500; // Increase volume to simulate trades
    return vol;
  }
};

MockEngine RealLiveExchange;
ExecutionEngine &LiveExchange = RealLiveExchange;

const char *tif_to_string(TimeInForce tif) {
  switch (tif) {
  case TimeInForce::GTC:
    return "GTC";
  case TimeInForce::IOC:
    return "IOC";
  case TimeInForce::FOK:
    return "FOK";
  default:
    return "UNKNOWN";
  }
}

const char *stp_to_string(SelfTradePrevention stp) {
  switch (stp) {
  case SelfTradePrevention::None:
    return "None";
  case SelfTradePrevention::CancelNew:
    return "CancelNew";
  case SelfTradePrevention::CancelOld:
    return "CancelOld";
  case SelfTradePrevention::CancelBoth:
    return "CancelBoth";
  default:
    return "UNKNOWN";
  }
}

void my_strategy() {
  // This is the BOP Language in action
  auto trade_limit =
      Buy(500_shares) / "FedRateCut"_mkt / YES + LimitPrice(Price(65_ticks)) |
      IOC | PostOnly;
  auto trade_market =
      Sell(1000_shares) / "FedRateCut"_mkt / NO + MarketPrice() | FOK |
      Iceberg(100_shares);

  // Dispatch to the C++ engine
  trade_limit >> LiveExchange;
  trade_market >> LiveExchange;

  std::cout << "Order 1 generated explicitly on stack.\n"
            << "Action: " << (trade_limit.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_limit.quantity << "\n"
            << "Market Hash: " << trade_limit.market.hash << "\n"
            << "Outcome: " << (trade_limit.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: " << trade_limit.price.raw << " ticks\n"
            << "TIF: " << tif_to_string(trade_limit.tif) << "\n"
            << "PostOnly: " << (trade_limit.post_only ? "true" : "false")
            << "\n"
            << "Iceberg Display Qty: " << trade_limit.display_qty << std::endl;

  std::cout << "\nOrder 2 generated explicitly on stack.\n"
            << "Action: " << (trade_market.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_market.quantity << "\n"
            << "Market Hash: " << trade_market.market.hash << "\n"
            << "Outcome: " << (trade_market.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: " << trade_market.price.raw << " (Market)\n"
            << "TIF: " << tif_to_string(trade_market.tif) << "\n"
            << "PostOnly: " << (trade_market.post_only ? "true" : "false")
            << "\n"
            << "Iceberg Display Qty: " << trade_market.display_qty << std::endl;

  // Advanced Conditional Order Chaining
  auto conditional =
      When(Market("FedRateCut"_mkt).Price(YES) > 60_ticks) >>
      (Sell(100_shares) / "FedRateCut"_mkt / YES + MarketPrice());
  conditional >> LiveExchange;

  std::cout << "\nConditional Order Generated.\n"
            << "Trigger Market Hash: "
            << conditional.condition.query.market.hash << "\n"
            << "Trigger Threshold: > " << conditional.condition.threshold
            << "\n"
            << "Action Execution: "
            << (conditional.order.is_buy ? "Buy " : "Sell ")
            << conditional.order.quantity << std::endl;
  // Pegged Order with Account Routing
  auto trade_pegged =
      Buy(300_shares) / "FedRateCut"_mkt / YES + Peg(Bid, Price(-1_ticks)) |
      GTC | "AlphaFund"_acc;
  trade_pegged >> LiveExchange;

  std::cout << "\nOrder 3 generated explicitly on stack.\n"
            << "Action: " << (trade_pegged.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_pegged.quantity << "\n"
            << "Market Hash: " << trade_pegged.market.hash << "\n"
            << "Outcome: " << (trade_pegged.outcome_yes ? "YES" : "NO") << "\n"
            << "Pegged Reference: "
            << (trade_pegged.algo_type == AlgoType::Peg
                    ? (std::get<PegData>(trade_pegged.algo_params).ref ==
                               ReferencePrice::Bid
                           ? "Bid"
                           : (std::get<PegData>(trade_pegged.algo_params).ref ==
                                      ReferencePrice::Ask
                                  ? "Ask"
                                  : "Mid"))
                    : "None")
            << "\n"
            << "Pegged Offset: "
            << (trade_pegged.algo_type == AlgoType::Peg
                    ? std::get<PegData>(trade_pegged.algo_params).offset.raw
                    : 0)
            << "\n"
            << "TIF: " << tif_to_string(trade_pegged.tif) << "\n"
            << "Account Routing Hash: " << trade_pegged.account_hash
            << std::endl;

  // Execution Algorithms (TWAP/VWAP)
  auto trade_twap =
      Sell(5000_shares) / "FedRateCut"_mkt / NO + MarketPrice() | TWAP(15_min);
  auto trade_vwap =
      Buy(10000_shares) / "FedRateCut"_mkt / YES + LimitPrice(Price(55_ticks)) |
      VWAP(0.10); // 10% participation
  trade_twap >> LiveExchange;
  trade_vwap >> LiveExchange;

  std::cout << "\nOrder 4 generated explicitly on stack.\n"
            << "Action: " << (trade_twap.is_buy ? "Buy " : "Sell ")
            << trade_twap.quantity << "\n"
            << "Market Hash: " << trade_twap.market.hash << "\n"
            << "Is TWAP: "
            << (trade_twap.algo_type == AlgoType::TWAP ? "true" : "false")
            << "\n"
            << "TWAP Duration (sec): "
            << (trade_twap.algo_type == AlgoType::TWAP
                    ? std::get<int64_t>(trade_twap.algo_params)
                    : 0)
            << std::endl;

  std::cout << "\nOrder 5 generated explicitly on stack.\n"
            << "Action: " << (trade_vwap.is_buy ? "Buy " : "Sell ")
            << trade_vwap.quantity << "\n"
            << "Market Hash: " << trade_vwap.market.hash << "\n"
            << "Is VWAP: "
            << (trade_vwap.algo_type == AlgoType::VWAP ? "true" : "false")
            << "\n"
            << "VWAP Max Participation: "
            << (trade_vwap.algo_type == AlgoType::VWAP
                    ? std::get<double>(trade_vwap.algo_params) * 100
                    : 0)
            << "%" << std::endl;

  // Bracket Order validation
  auto trade_bracket = (Buy(100_shares) / "MarsLanding"_mkt / YES +
                        LimitPrice(Price(50_ticks))) &
                       TakeProfit(Price(70_ticks)) & StopLoss(Price(40_ticks));
  trade_bracket >> LiveExchange;

  std::cout << "\nOrder 6 (Bracket) generated explicitly on stack.\n"
            << "Action: " << (trade_bracket.is_buy ? "Buy " : "Sell ")
            << trade_bracket.quantity << "\n"
            << "Market Hash: " << trade_bracket.market.hash << "\n"
            << "Limit Price: " << trade_bracket.price.raw << " ticks\n"
            << "Take Profit: " << trade_bracket.tp_price.raw << " ticks\n"
            << "Stop Loss: " << trade_bracket.sl_price.raw << " ticks"
            << std::endl;

  // OCO Order and Trailing Stop validation
  auto oco_order = (Sell(100_shares) / "MarsLanding"_mkt / YES +
                    LimitPrice(Price(80_ticks))) ||
                   (Sell(100_shares) / "MarsLanding"_mkt / YES +
                        LimitPrice(Price(45_ticks)) |
                    TrailingStop(Price(5_ticks)));
  oco_order >> LiveExchange;

  std::cout
      << "\nOrder 7 (OCO with Trailing Stop) generated explicitly on stack.\n"
      << "Leg 1 Action: " << (oco_order.order1.is_buy ? "Buy " : "Sell ")
      << oco_order.order1.quantity << "\n"
      << "Leg 1 Price: " << oco_order.order1.price.raw << " ticks\n"
      << "Leg 2 Action: " << (oco_order.order2.is_buy ? "Buy " : "Sell ")
      << oco_order.order2.quantity << "\n"
      << "Leg 2 Price: " << oco_order.order2.price.raw << " ticks\n"
      << "Leg 2 Trailing: "
      << (oco_order.order2.algo_type == AlgoType::Trailing ? "true" : "false")
      << " (Amount: "
      << (oco_order.order2.algo_type == AlgoType::Trailing
              ? std::get<int64_t>(oco_order.order2.algo_params)
              : 0)
      << " ticks)" << std::endl;

  // Self-Trade Prevention validation
  auto trade_stp =
      Buy(200_shares) / "FedRateCut"_mkt / YES + LimitPrice(Price(60_ticks)) |
      STP;
  auto trade_stp_custom =
      Sell(200_shares) / "FedRateCut"_mkt / YES + LimitPrice(Price(60_ticks)) |
      CancelOld;
  trade_stp >> LiveExchange;
  trade_stp_custom >> LiveExchange;

  std::cout << "\nOrder 8 (STP) generated explicitly on stack.\n"
            << "Action: " << (trade_stp.is_buy ? "Buy " : "Sell ")
            << trade_stp.quantity << " shares\n"
            << "STP Mode (Default): " << stp_to_string(trade_stp.stp) << "\n"
            << "STP Mode (Custom): " << stp_to_string(trade_stp_custom.stp)
            << std::endl;

  // Atomic Order Batching
  std::cout << "\nAtomic Order Batching demonstration:\n";
  Batch(
      {Buy(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(Price(50_ticks)),
       Sell(50_shares) / "MarsLanding"_mkt / NO + MarketPrice()}) >>
      LiveExchange;
  std::cout << "Sent 2 orders as a single atomic batch.\n";

  // New: Complex Multi-Signal Trigger (Logical Condition Composition)
  auto multi_signal = When(Market("BTC").Price(YES) > 60_ticks &&
                           Market("ETH").Price(YES) < 40_ticks) >>
                      Buy(100) / "BTC" / YES;
  multi_signal >> LiveExchange;

  std::cout << "\nOrder 8 (Complex Multi-Signal) generated.\n"
            << "Condition Type: BTC.Price > 0.60 AND ETH.Price < 0.40\n"
            << "Action: Buy 100 BTC YES @ Market" << std::endl;
}

void risk_aware_strategy() {
  std::cout << "\nRunning Risk-Aware Strategy...\n";

  // The new Position() and Balance() keywords in action using the clean
  // pipeline DSL
  auto risk_aware_order =
      When(Position("MarsLanding"_mkt) < 1000 && Balance() > 5000) >>
      (Buy(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(Price(50_ticks)));

  risk_aware_order >> LiveExchange;
}

void pro_strategy() {
  std::cout << "\nRunning Professional Strategy...\n";

  // 1. Spread Trading
  auto spread_trade = Buy(100_shares) / (Market("BTC") - Market("ETH")) / YES;
  spread_trade >> LiveExchange;

  std::cout << "Spread Order: Buy 100 BTC-ETH Spread YES" << std::endl;

  // 2. Risk-Gated Execution with Market Depth
  auto gated_order =
      When(Exposure() < 50000 && Market("BTC").Spread() < 5_ticks) >>
      (Buy(500_shares) / "BTC" / YES + MarketPrice());

  gated_order >> LiveExchange;

  std::cout << "Gated Order: Buy 500 BTC YES if Exposure < 50k and Spread < 5"
            << std::endl;
}

void arbitrage_strategy() {
  std::cout << "\nRunning Multi-Market Arbitrage Strategy...\n";

  using namespace bop::exchanges;

  // Cross-market price comparison
  // Buy on Kalshi if it's cheaper than Polymarket
  auto arb_logic =
      When(Market("BTC", kalshi).Price(YES) <
           Market("BTC", polymarket).Price(YES)) >>
      (Buy(100_shares) / Market("BTC", kalshi) / YES + MarketPrice());

  arb_logic >> LiveExchange;

  std::cout << "Arb Check: Kalshi BTC ("
            << kalshi.get_price("BTC"_mkt, true).raw << ") vs Polymarket BTC ("
            << polymarket.get_price("BTC"_mkt, true).raw << ")" << std::endl;
}

void auth_demo() {
  std::cout << "\nRunning Authentication Demo...\n";

  using namespace bop::exchanges;

  // Set Kalshi Credentials
  kalshi.set_credentials({"my_api_key", "my_secret_key", "my_passphrase", ""});

  std::string k_sign = kalshi.sign_request("GET", "/v2/exchange/status");
  std::cout << "Kalshi Signature: " << k_sign << std::endl;

  // Set Polymarket Credentials
  polymarket.set_credentials({"", "0x_my_private_key", "", "0x_my_address"});

  std::string p_sign =
      polymarket.sign_request("POST", "/orders", "{\"qty\":10}");
  std::cout << "Polymarket Signature: " << p_sign << std::endl;
}

void algo_logic_demo() {
  std::cout << "\nRunning Execution Algorithm Logic Demo...\n";
  using namespace bop::exchanges;

  // 1. TWAP Demo (5 second TWAP)
  auto twap_order =
      Sell(1000_shares) / Market("BTC", kalshi) / NO + MarketPrice() |
      TWAP(5_sec);
  twap_order >> LiveExchange;

  // 2. Trailing Stop Demo
  auto trailing_stop = Sell(500_shares) / Market("BTC", polymarket) / YES +
                       TrailingStop(Price(5_ticks));
  trailing_stop >> LiveExchange;

  // 3. Pegging Demo
  auto peg_order =
      Buy(200_shares) / Market("ETH", kalshi) / YES + Peg(Bid, Price(1_ticks));
  peg_order >> LiveExchange;

  // 4. VWAP / Participation Demo (10% participation)
  auto vwap_order =
      Buy(500_shares) / Market("BTC", polymarket) / YES | VWAP(0.1);
  vwap_order >> LiveExchange;

  std::cout << "Algo Manager now tracking " << GlobalAlgoManager.active_count()
            << " algorithms. Handing over to Execution Engine." << std::endl;
}

void streaming_demo() {
  std::cout << "\nRunning Streaming & WebSocket Demo...\n";
  using namespace bop::exchanges;

  // 1. Subscribe to Kalshi Orderbook
  // The MarketId("BTC") will automatically trigger the WS subscription logic
  kalshi.ws_subscribe_orderbook(MarketId("BTC"), [](const OrderBook &ob) {
    if (!ob.bids.empty()) {
      std::cout << "[CALLBACK] Kalshi BTC Book Update -> Best Bid: "
                << ob.bids[0].price << std::endl;
    }
  });

  // 2. Subscribe to PolyMarket Trades
  polymarket.ws_subscribe_trades(MarketId("BTC"), [](Price p, int64_t qty) {
    std::cout << "[CALLBACK] Poly BTC Trade -> " << qty << " @ " << p
              << std::endl;
  });

  std::cout << "Subscribed to live feeds. (Simulation mode)\n";
}

int main() {
  // Register backends for real-time tracking
  RealLiveExchange.register_backend(&kalshi);
  RealLiveExchange.register_backend(&polymarket);

  my_strategy();
  risk_aware_strategy();
  pro_strategy();
  arbitrage_strategy();
  auth_demo();
  algo_logic_demo();
  streaming_demo();

  std::cout << "\n[MAIN] Starting Global Execution Engine for 5 seconds..." << std::endl;
  
  // Start a thread to stop the engine after 5 seconds for demonstration purposes
  std::thread stopper([]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "\n[MAIN] Stopping engine..." << std::endl;
    LiveExchange.stop();
  });

  LiveExchange.run();
  
  if (stopper.joinable()) {
    stopper.join();
  }

  return 0;
}
