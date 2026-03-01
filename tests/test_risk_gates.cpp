#include "bop.hpp"
#include <cassert>
#include <iostream>

using namespace bop;

void test_risk_gates() {
  std::cout << "Testing Risk & Safety Gates DSL..." << std::endl;

  // 1. Declarative Invariants
  Strategy.invariant(MaxPosition(1000) && DailyLossLimit(500_usd));

  // 2. Automatic Circuit Breakers
  OnRiskViolation() >> (CancelAll() | ClosePositions());

  std::cout << "Risk gates configured successfully." << std::endl;
}

int main() {
  test_risk_gates();
  std::cout << "All Risk Gates Tests Passed!" << std::endl;
  return 0;
}
