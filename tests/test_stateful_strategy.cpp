#include "bop.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace bop;
using namespace std::chrono_literals;

int main() {
  std::cout << "Starting Stateful Strategy Tests..." << std::endl;

  // Test 1: After trigger
  // Note: We need a mock engine or a real one to test this properly.
  // For now, let's just test compilation.

  auto order1 = Buy(100_shares) / "AAPL"_mkt / YES;
  auto order2 = Buy(200_shares) / "TSLA"_mkt / YES;

  auto step1 = After(1_sec) >> order1;
  auto step2 = OnFill(order1) >> order2;
  auto step3 = Every(5_sec) >> [](ExecutionEngine &engine) {
    std::cout << "Heartbeat tick..." << std::endl;
  };

  std::cout << "Compilation successful for stateful DSL constructs."
            << std::endl;

  return 0;
}
