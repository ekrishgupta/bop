#pragma once

// Outcome tags
struct YES_t {};
static constexpr YES_t YES;
struct NO_t {};
static constexpr NO_t NO;

// The Order "State Machine"
struct Order {
  const char *market;
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

// Intermediate DSL structure: Market Bound
struct MarketBoundOrder {
  int quantity;
  bool is_buy;
  const char *market;
};

constexpr MarketBoundOrder operator/(const Buy &b, const char *market) {
  return MarketBoundOrder{b.quantity, true, market};
}

constexpr MarketBoundOrder operator/(const Sell &s, const char *market) {
  return MarketBoundOrder{s.quantity, false, market};
}

// Intermediate DSL structure: Outcome Bound
struct OutcomeBoundOrder {
  int quantity;
  bool is_buy;
  const char *market;
  bool outcome_yes;
};

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, YES_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, true};
}

constexpr OutcomeBoundOrder operator/(const MarketBoundOrder &m, NO_t) {
  return OutcomeBoundOrder{m.quantity, m.is_buy, m.market, false};
}

// OutcomeBoundOrder + Price -> Order
constexpr Order operator+(const OutcomeBoundOrder &o, double price) {
  return Order{o.market, o.quantity, o.is_buy, o.outcome_yes, price};
}
