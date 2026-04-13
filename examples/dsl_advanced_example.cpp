#include "core/engine.hpp"
#include "core/logic.hpp"
#include <iostream>
#include <thread>

using namespace bop;
using namespace std::chrono_literals;

struct MockBackend : public MarketBackend {
  Price p;
  MockBackend() : p(0.50_usd) {}
  std::string name() const override { return "Mock"; }
  Price get_price(MarketId, OutcomeId) const override { return p; }
  Price get_depth(MarketId, OutcomeId) const override { return p; }
  std::string create_order(const Order &o) const override {
    std::cout << "[MOCK] Executing " << (o.is_buy ? "BUY" : "SELL") << " "
              << o.quantity << " shares" << std::endl;
    return "ord_123";
  }
  Price get_balance() const override { return 1000_usd; }
};

int main() {
  LiveExecutionEngine engine;
  MockBackend mock;
  engine.register_backend(&mock);

  std::cout << "--- BOP Advanced DSL Demo ---" << std::endl;

  // 1. Temporal Trigger: Refresh quoting every 1 second
  std::cout << "[DEMO] Setting up Every(1s) Quote..." << std::endl;
  Every(1s) >> Quote(Shares(10)) / Market("BTC") >> engine;

  // 2. Chained Execution: Buy now, then Sell after fill
  // Note: In this mock, filling doesn't happen automatically unless we trigger
  // it.
  std::cout << "[DEMO] Setting up Chained execution (Buy -> Then Sell)..."
            << std::endl;
  (When(Market("ETH").Midpoint() < 0.60_usd) >>
   Buy(Shares(10)) / Market("ETH") / YES) >>
      (Sell(Shares(10)) / Market("ETH") / YES) >> engine;

  // 3. Fair Price Logic: Capture discrepancy
  std::cout << "[DEMO] Checking FairPrice logic..." << std::endl;
  auto arb_cond = When(Market("BTC").FairPrice() > 0.55_usd);
  mock.p = 0.60_usd; // Set price to meet condition

  if (arb_cond.condition.eval()) {
    std::cout << "[SUCCESS] Fair price condition met: " << mock.p << " > 0.55"
              << std::endl;
  }

  // 4. Proportional Sizing (Syntax test)
  std::cout << "[DEMO] Testing % sizing syntax..." << std::endl;
  auto order = Buy(Shares(100)) * 5_pct / Market("SOL") / YES;
  std::cout << "[INFO] Order created (sizing logic applied in engine hot-path)"
            << std::endl;

  // Run engine tick loop for a bit to see 'Every' triggers
  std::cout << "[DEMO] Running engine for 3 ticks..." << std::endl;
  for (int i = 0; i < 3; ++i) {
    std::cout << "Tick " << i << "..." << std::endl;
    GlobalAlgoManager.tick(engine);
    std::this_thread::sleep_for(1s);
  }

  return 0;
}
