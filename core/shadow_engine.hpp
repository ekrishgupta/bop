#pragma once

#include "engine.hpp"
#include <iostream>

namespace bop {

/**
 * @brief An Execution Engine that simulates execution while receiving live data.
 * Ideal for "Paper Trading" or validating strategies in production environments.
 */
class ShadowExecutionEngine : public LiveExecutionEngine {
public:
    ShadowExecutionEngine() : LiveExecutionEngine() {
        std::cout << "[SHADOW] Initialized in PAPER TRADING mode." << std::endl;
    }

    void execute_order(const Order &o) override {
        // We don't send to backend, we simulate a fill immediately
        std::cout << "[SHADOW] Simulating order execution for " << o.market.ticker << std::endl;
        
        static int shadow_id_seq = 1;
        std::string shadow_id = "shadow_" + std::to_string(shadow_id_seq++);
        
        track_order(shadow_id, o);
        
        // Simulate a fill after a small delay (or immediately)
        Price fill_price = o.price.raw > 0 ? o.price : get_price(o.market, o.outcome_yes);
        if (fill_price.raw == 0) fill_price = Price::from_usd(0.5);
        
        add_order_fill(shadow_id, o.quantity, fill_price);
        update_order_status(shadow_id, OrderStatus::Filled);
    }

    void execute_cancel(const std::string &id) override {
        std::cout << "[SHADOW] Simulating cancel for " << id << std::endl;
        update_order_status(id, OrderStatus::Cancelled);
    }

    void execute_batch(const std::vector<Order> &orders) override {
        std::cout << "[SHADOW] Simulating batch of " << orders.size() << " orders." << std::endl;
        for (const auto& o : orders) execute_order(o);
    }
};

} // namespace bop
