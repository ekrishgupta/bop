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

void my_strategy() {
  // This is the BOP Language in action
  auto trade_limit =
      Buy(500_shares) / "FedRateCut"_mkt / YES + LimitPrice(0.65) | IOC |
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
            << "Price: $" << trade_limit.price << "\n"
            << "TIF: " << tif_to_string(trade_limit.tif) << "\n"
            << "PostOnly: " << (trade_limit.post_only ? "true" : "false")
            << "\n"
            << "Iceberg Display Qty: " << trade_limit.display_qty << std::endl;

  std::cout << "\nOrder 2 generated explicitly on stack.\n"
            << "Action: " << (trade_market.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_market.quantity << "\n"
            << "Market Hash: " << trade_market.market.hash << "\n"
            << "Outcome: " << (trade_market.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: $" << trade_market.price << " (Market)\n"
            << "TIF: " << tif_to_string(trade_market.tif) << "\n"
            << "PostOnly: " << (trade_market.post_only ? "true" : "false")
            << "\n"
            << "Iceberg Display Qty: " << trade_market.display_qty << std::endl;

  // Advanced Conditional Order Chaining
  auto conditional =
      When(Market("FedRateCut"_mkt).Price(YES) > 0.60) >>
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
  // Pegged Order
  auto trade_pegged =
      Buy(300_shares) / "FedRateCut"_mkt / YES + Peg(Bid, -0.01) | GTC;
  trade_pegged >> LiveExchange;

  std::cout << "\nOrder 3 generated explicitly on stack.\n"
            << "Action: " << (trade_pegged.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_pegged.quantity << "\n"
            << "Market Hash: " << trade_pegged.market.hash << "\n"
            << "Outcome: " << (trade_pegged.outcome_yes ? "YES" : "NO") << "\n"
            << "Pegged Reference: "
            << (trade_pegged.pegged_ref == ReferencePrice::Bid
                    ? "Bid"
                    : (trade_pegged.pegged_ref == ReferencePrice::Ask ? "Ask"
                                                                      : "Mid"))
            << "\n"
            << "Pegged Offset: " << trade_pegged.peg_offset << "\n"
            << "TIF: " << tif_to_string(trade_pegged.tif) << std::endl;

  // Execution Algorithms (TWAP/VWAP)
  auto trade_twap =
      Sell(5000_shares) / "FedRateCut"_mkt / NO + MarketPrice() | TWAP(15_min);
  auto trade_vwap =
      Buy(10000_shares) / "FedRateCut"_mkt / YES + LimitPrice(0.55) |
      VWAP(0.10); // 10% participation
  trade_twap >> LiveExchange;
  trade_vwap >> LiveExchange;

  std::cout << "\nOrder 4 generated explicitly on stack.\n"
            << "Action: " << (trade_twap.is_buy ? "Buy " : "Sell ")
            << trade_twap.quantity << "\n"
            << "Market Hash: " << trade_twap.market.hash << "\n"
            << "Is TWAP: " << (trade_twap.is_twap ? "true" : "false") << "\n"
            << "TWAP Duration (sec): " << trade_twap.twap_duration.count()
            << std::endl;

  std::cout << "\nOrder 5 generated explicitly on stack.\n"
            << "Action: " << (trade_vwap.is_buy ? "Buy " : "Sell ")
            << trade_vwap.quantity << "\n"
            << "Market Hash: " << trade_vwap.market.hash << "\n"
            << "Is VWAP: " << (trade_vwap.is_vwap ? "true" : "false") << "\n"
            << "VWAP Max Participation: " << trade_vwap.vwap_participation * 100
            << "%" << std::endl;
}

int main() {
  my_strategy();
  return 0;
}
