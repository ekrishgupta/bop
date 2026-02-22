#include "bop.hpp"
#include <iostream>

ExecutionEngine LiveExchange; // Global instance for testing

void my_strategy() {
  // This is the BOP Language in action
  auto trade_limit =
      Buy(500_shares) / "FedRateCut"_mkt / YES + LimitPrice(0.65);
  auto trade_market = Sell(200_shares) / "FedRateCut"_mkt / NO + MarketPrice();

  // Dispatch to the C++ engine
  trade_limit >> LiveExchange;
  trade_market >> LiveExchange;

  std::cout << "Order 1 generated explicitly on stack.\n"
            << "Action: " << (trade_limit.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_limit.quantity << "\n"
            << "Market Hash: " << trade_limit.market.hash << "\n"
            << "Outcome: " << (trade_limit.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: $" << trade_limit.price << std::endl;

  std::cout << "\nOrder 2 generated explicitly on stack.\n"
            << "Action: " << (trade_market.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade_market.quantity << "\n"
            << "Market Hash: " << trade_market.market.hash << "\n"
            << "Outcome: " << (trade_market.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: $" << trade_market.price << " (Market)" << std::endl;
}

int main() {
  my_strategy();
  return 0;
}
