#include "bop.hpp"
#include <iostream>

ExecutionEngine LiveExchange; // Global instance for testing

void my_strategy() {
  // This is the BOP Language in action
  auto trade = Buy(500_shares) / "FedRateCut"_mkt / YES + 0.65;

  // Dispatch to the C++ engine
  trade >> LiveExchange;

  std::cout << "Order generated explicitly on stack.\n"
            << "Action: " << (trade.is_buy ? "Buy " : "Sell ") << "\n"
            << "Quantity: " << trade.quantity << "\n"
            << "Market Hash: " << trade.market.hash << "\n"
            << "Outcome: " << (trade.outcome_yes ? "YES" : "NO") << "\n"
            << "Price: $" << trade.price << std::endl;
}

int main() {
  my_strategy();
  return 0;
}
