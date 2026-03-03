#include "bop.hpp"
#include "core/backtest.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace bop;
using namespace std::chrono_literals;

int main() {
  std::cout << "Starting Stateful Strategy Functional Tests..." << std::endl;

  BacktestExecutionEngine engine;
  BacktestMarketBackend backend("Mock");
  engine.add_backend(backend);

  MarketId mkt("AAPL");
  backend.set_price(mkt, 150_usd, 151_usd);

  // --- Test 1: OnFill Trigger ---
  std::cout << "Test 1: OnFill Trigger..." << std::endl;

  auto order1 = Buy(100_shares) / mkt / YES;
  auto order2 = Buy(200_shares) / mkt / YES;

  bool order2_executed = false;

  // Register step: OnFill(order1) >> order2
  // We'll use a callback to verify execution
  auto step1 =
      OnFill(order1) >> [&order2_executed, order2](ExecutionEngine &eng) {
        std::cout << "Triggered order2 execution!" << std::endl;
        order2_executed = true;
        order2 >> eng;
      };

  step1 >> engine;

  // Submit order1 manually
  order1 >> engine;

  // Initially order2 should not be executed
  assert(!order2_executed);

  // Simulate order1 fill
  engine.add_order_fill(order1.order_id, 100_shares, 150_usd);

  // Tick the engine
  GlobalAlgoManager.tick(engine);

  // Now order2 should have been triggered
  if (order2_executed) {
    std::cout << "Test 1 Passed: OnFill triggered successfully." << std::endl;
  } else {
    std::cout << "Test 1 Failed: OnFill not triggered." << std::endl;
    return 1;
  }

  // --- Test 2: Timer Trigger (Every) ---
  std::cout << "Test 2: Timer Trigger (Every)..." << std::endl;

  int heartbeat_count = 0;
  auto step2 = Every(100ms) >>
               [&heartbeat_count](ExecutionEngine &) { heartbeat_count++; };

  step2 >> engine;

  // Simulate time passing (in real time since TimerTrigger uses
  // system_clock::now)

  GlobalAlgoManager.tick(engine);
  // Wait a bit
  std::this_thread::sleep_for(150ms);
  GlobalAlgoManager.tick(engine);

  std::this_thread::sleep_for(150ms);
  GlobalAlgoManager.tick(engine);

  std::cout << "Heartbeat count: " << heartbeat_count << std::endl;
  // It should have triggered at least twice (+1 if first tick immediately
  // triggers)
  if (heartbeat_count >= 2) {
    std::cout << "Test 2 Passed: Every(100ms) triggered successfully."
              << std::endl;
  } else {
    std::cout << "Test 2 Failed: Every (heartbeat count: " << heartbeat_count
              << ")" << std::endl;
    return 1;
  }

  std::cout << "All Stateful Strategy Tests Passed!" << std::endl;

  return 0;
}
