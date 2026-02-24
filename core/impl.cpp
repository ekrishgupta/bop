#include "order_tracker.hpp"
#include "algo_manager.hpp"
#include "algo.hpp"
#include <iostream>

namespace bop {

AlgoManager GlobalAlgoManager;
OrderTracker GlobalOrderTracker;

void StreamingMarketBackend::notify_fill(const std::string &id, int qty, Price price) {
    if (engine_) {
        engine_->add_order_fill(id, qty, price);
    }
}

void StreamingMarketBackend::notify_status(const std::string &id, OrderStatus status) {
    if (engine_) {
        engine_->update_order_status(id, status);
    }
}

} // namespace bop
