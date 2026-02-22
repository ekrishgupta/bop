#pragma once

#include <string>

// Outcome tags
struct YES_t {}; static constexpr YES_t YES;
struct NO_t {}; static constexpr NO_t NO;

// The Order "State Machine"
struct Order {
    std::string market;
    int quantity;
    bool is_buy;
    bool outcome_yes;
    double price;
};
