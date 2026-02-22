#include "bop.hpp"
#include <iostream>

ExecutionEngine LiveExchange; // Global instance for testing

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
      Buy(500_shares) / "FedRateCut"_mkt / YES + LimitPrice(65_ticks) | IOC |
      PostOnly;
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
            << "Price: " << trade_limit.price << " ticks\n"
            << "TIF: " << tif_to_string(trade_limit.tif) << "\n"
            << "PostOnly: " << (trade_limit.post_only ? "true" : "false")
            << "\n"
            << "Iceberg Display Qty: " << trade_limit.display_qty << std::endl;

  std::cout << "\nOrder 2 generated explicitly on stack.\n"
            << "Action: " << (trade_market.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_market.quantity << "\n"
            << "Market Hash: " << trade_market.market.hash << "\n"
            << "Outcome: " << (trade_market.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: " << trade_market.price << " (Market)\n"
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
      Buy(300_shares) / "FedRateCut"_mkt / YES + Peg(Bid, -1_ticks) | GTC |
      "AlphaFund"_acc;
  trade_pegged >> LiveExchange;

  std::cout
      << "\nOrder 3 generated explicitly on stack.\n"
      << "Action: " << (trade_pegged.is_buy ? "Buy " : "Sell ") << "\n"
      << "Quantity: " << trade_pegged.quantity << "\n"
      << "Market Hash: " << trade_pegged.market.hash << "\n"
      << "Outcome: " << (trade_pegged.outcome_yes ? "YES" : "NO") << "\n"
      << "Pegged Reference: "
      << (trade_pegged.algo_type == AlgoType::Peg
              ? (trade_pegged.peg.ref == ReferencePrice::Bid
                     ? "Bid"
                     : (trade_pegged.peg.ref == ReferencePrice::Ask ? "Ask"
                                                                    : "Mid"))
              : "None")
      << "\n"
      << "Pegged Offset: "
      << (trade_pegged.algo_type == AlgoType::Peg ? trade_pegged.peg.offset : 0)
      << "\n"
      << "TIF: " << tif_to_string(trade_pegged.tif) << "\n"
      << "Account Routing Hash: " << trade_pegged.account_hash << std::endl;

  // Execution Algorithms (TWAP/VWAP)
  auto trade_twap =
      Sell(5000_shares) / "FedRateCut"_mkt / NO + MarketPrice() | TWAP(15_min);
  auto trade_vwap =
      Buy(10000_shares) / "FedRateCut"_mkt / YES + LimitPrice(55_ticks) |
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
                    ? trade_twap.twap_duration_sec
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
                    ? trade_vwap.vwap_participation * 100
                    : 0)
            << "%" << std::endl;

  // Bracket Order validation
  auto trade_bracket =
      (Buy(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(50_ticks)) &
      TakeProfit(70_ticks) & StopLoss(40_ticks);
  trade_bracket >> LiveExchange;

  std::cout << "\nOrder 6 (Bracket) generated explicitly on stack.\n"
            << "Action: " << (trade_bracket.is_buy ? "Buy " : "Sell ")
            << trade_bracket.quantity << "\n"
            << "Market Hash: " << trade_bracket.market.hash << "\n"
            << "Limit Price: " << trade_bracket.price << " ticks\n"
            << "Take Profit: " << trade_bracket.tp_price << " ticks\n"
            << "Stop Loss: " << trade_bracket.sl_price << " ticks" << std::endl;

  // OCO Order and Trailing Stop validation
  auto take_profit =
      Sell(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(80_ticks);
  auto stop_loss =
      Sell(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(45_ticks) |
      TrailingStop(5_ticks);
  auto oco_order = take_profit || stop_loss;
  oco_order >> LiveExchange;

  std::cout
      << "\nOrder 7 (OCO with Trailing Stop) generated explicitly on stack.\n"
      << "Leg 1 Action: " << (oco_order.order1.is_buy ? "Buy " : "Sell ")
      << oco_order.order1.quantity << "\n"
      << "Leg 1 Price: " << oco_order.order1.price << " ticks\n"
      << "Leg 2 Action: " << (oco_order.order2.is_buy ? "Buy " : "Sell ")
      << oco_order.order2.quantity << "\n"
      << "Leg 2 Price: " << oco_order.order2.price << " ticks\n"
      << "Leg 2 Trailing: "
      << (oco_order.order2.algo_type == AlgoType::Trailing ? "true" : "false")
      << " (Amount: "
      << (oco_order.order2.algo_type == AlgoType::Trailing
              ? oco_order.order2.trail_amount
              : 0)
      << " ticks)" << std::endl;

  // Self-Trade Prevention validation
  auto trade_stp =
      Buy(200_shares) / "FedRateCut"_mkt / YES + LimitPrice(60_ticks) | STP;
  auto trade_stp_custom =
      Sell(200_shares) / "FedRateCut"_mkt / YES + LimitPrice(60_ticks) |
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
  std::initializer_list<bop::Order> batch = {
      Buy(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(50_ticks),
      Sell(50_shares) / "MarsLanding"_mkt / NO + MarketPrice()};
  batch >> LiveExchange;
  std::cout << "Sent 2 orders as a single atomic batch.\n";

  // New: Complex Multi-Signal Trigger (Logical Condition Composition)
  auto multi_signal = When(Market("BTC").Price(YES) > 0.60 &&
                           Market("ETH").Price(YES) < 0.40) >>
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
      (Buy(100_shares) / "MarsLanding"_mkt / YES + LimitPrice(50_ticks));

  risk_aware_order >> LiveExchange;
}

int main() {
  my_strategy();
  risk_aware_strategy();
  return 0;
}
