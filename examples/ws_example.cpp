#include "core/engine.hpp"
#include "exchanges/kalshi/kalshi.hpp"
#include <iostream>

using namespace bop;
using namespace bop::exchanges;

int main() {
  // 1. Traditional polling DSL (now potentially backended by WS cache)
  auto order1 = Buy(100) / kalshi / "Mars_MKT" / YES + LimitPrice(50_cents);

  // 2. Event-driven high-frequency logic using WebSockets
  Market("Mars_MKT", kalshi).OnOrderbook([](const OrderBook &ob) {
    std::cout << "[HF] Received orderbook update for Mars!" << std::endl;
    if (!ob.asks.empty() && ob.asks[0].price < 45_cents) {
      std::cout << "[HF] Price dropped! Executing HFT trade." << std::endl;
      // Immediate dispatch
      Buy(500) / kalshi / "Mars_MKT" / YES >> LiveExchange;
    }
  });

  // 3. Conditional order that can be evaluated in a loop
  When(Market("Mars_MKT", kalshi).Price(YES) < 40_cents) >>
      (Buy(100) / kalshi / "Mars_MKT" / YES) >> LiveExchange;

  return 0;
}
