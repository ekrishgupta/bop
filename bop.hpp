#pragma once

#include <string>

// Outcome tags
struct YES_t {};
static constexpr YES_t YES;
struct NO_t {};
static constexpr NO_t NO;

// The Order "State Machine"
struct Order {
  std::string market;
  int quantity;
  bool is_buy;
  bool outcome_yes;
  double price;
};

// Action Types
struct Buy {
  int quantity;
  constexpr explicit Buy(int q) : quantity(q) {}
};

struct Sell {
  int quantity;
  constexpr explicit Sell(int q) : quantity(q) {}
};

// User-Defined Literals for quantities
constexpr int operator"" _shares(unsigned long long int v) {
  return static_cast<int>(v);
}
