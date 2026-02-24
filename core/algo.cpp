#include "algo.hpp"
#include "engine.hpp"
#include <iostream>
#include <algorithm>

namespace bop {

// TWAPAlgo Implementation
TWAPAlgo::TWAPAlgo(const Order &o) {
    parent_order = o;
    duration_sec = std::get<int64_t>(o.algo_params);
    total_qty = o.quantity;
    start_time_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

bool TWAPAlgo::tick(ExecutionEngine &engine) {
    int64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    double elapsed_sec = (now_ns - start_time_ns) / 1e9;

    if (elapsed_sec >= duration_sec) {
        int remaining = total_qty - filled_qty;
        if (remaining > 0) {
            dispatch_slice(remaining, engine);
            filled_qty += remaining;
        }
        std::cout << "[ALGO] TWAP Completed for " << parent_order.market.ticker << std::endl;
        return true;
    }

    // Slice at most once every 5 seconds or if we are at the very beginning
    bool interval_passed = (last_slice_time_ns == 0) || (now_ns - last_slice_time_ns > 5e9);
    
    if (interval_passed) {
        double target_qty = (elapsed_sec / (double)duration_sec) * total_qty;
        int to_fill = static_cast<int>(target_qty) - filled_qty;

        if (to_fill > 0) {
            dispatch_slice(to_fill, engine);
            filled_qty += to_fill;
            last_slice_time_ns = now_ns;
        }
    }
    return false;
}

void TWAPAlgo::dispatch_slice(int qty, ExecutionEngine &engine) {
    Order slice = parent_order;
    slice.quantity = qty;
    slice.algo_type = AlgoType::None;

    if (slice.backend) {
        std::cout << "[ALGO] TWAP Slice: " << qty << " shares to " << slice.backend->name() << std::endl;
        slice.backend->create_order(slice);
    }
}

// TrailingStopAlgo Implementation
TrailingStopAlgo::TrailingStopAlgo(const Order &o) {
    parent_order = o;
    trail_amount = Price(std::get<int64_t>(o.algo_params));
    best_price = Price(0);
}

bool TrailingStopAlgo::tick(ExecutionEngine &engine) {
    Price current_price = engine.get_price(parent_order.market, parent_order.outcome_yes);
    if (current_price.raw == 0) return false;

    int64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();

    if (!activated) {
        std::cout << "[ALGO] Trailing Stop Activated for " << parent_order.market.ticker << " at " << current_price << " (Trail: " << trail_amount << ")" << std::endl;
        best_price = current_price;
        activated = true;
        last_log_time_ns = now_ns;
        return false;
    }

    bool price_improved = parent_order.is_buy ? (current_price < best_price) : (current_price > best_price);

    if (price_improved) {
        best_price = current_price;
        std::cout << "[ALGO] Trailing Stop Updated Best Price: " << best_price << " (Current: " << current_price << ")" << std::endl;
    }

    Price stop_price = parent_order.is_buy ? (best_price + trail_amount) : (best_price - trail_amount);

    // Log status every 10 seconds
    if (now_ns - last_log_time_ns > 10e9) {
        std::cout << "[ALGO] Trailing Stop [" << parent_order.market.ticker << "] Current: " << current_price << " Best: " << best_price << " Stop: " << stop_price << std::endl;
        last_log_time_ns = now_ns;
    }

    bool triggered = parent_order.is_buy ? (current_price >= stop_price) : (current_price <= stop_price);

    if (triggered) {
        std::cout << "[ALGO] Trailing Stop Triggered! Market: " << parent_order.market.ticker << " at " << current_price << " (Best: " << best_price << ", Stop: " << stop_price << ")" << std::endl;
        Order market_order = parent_order;
        market_order.algo_type = AlgoType::None;
        market_order.price = Price(0); // Market order
        if (market_order.backend) {
            market_order.backend->create_order(market_order);
        }
        return true;
    }
    return false;
}

// PegAlgo Implementation
PegAlgo::PegAlgo(const Order &o) {
    parent_order = o;
    auto data = std::get<PegData>(o.algo_params);
    offset = data.offset;
    ref = data.ref;
}

bool PegAlgo::tick(ExecutionEngine &engine) {
    int64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    Price bbo_price;
    if (ref == ReferencePrice::Mid) {
        Price bid = engine.get_depth(parent_order.market, true);
        Price ask = engine.get_depth(parent_order.market, false);
        if (bid.raw == 0 || ask.raw == 0) return false;
        bbo_price = Price((bid.raw + ask.raw) / 2);
    } else {
        bool is_bid = (ref == ReferencePrice::Bid);
        bbo_price = engine.get_depth(parent_order.market, is_bid);
    }

    if (bbo_price.raw == 0) return false;

    Price target_price = bbo_price + offset;

    if (target_price != last_quoted_price) {
        // Throttle updates to at most once every 500ms
        if (last_update_time_ns != 0 && (now_ns - last_update_time_ns < 500e6)) {
            return false;
        }

        if (!active_order_id.empty() && parent_order.backend) {
            parent_order.backend->cancel_order(active_order_id);
        }

        Order slice = parent_order;
        slice.price = target_price;
        slice.algo_type = AlgoType::None;

        if (slice.backend) {
            std::cout << "[ALGO] Pegging " << parent_order.market.ticker << " to " << target_price << " (Offset: " << offset << ")" << std::endl;
            active_order_id = slice.backend->create_order(slice);
        }
        last_quoted_price = target_price;
        last_update_time_ns = now_ns;
    }
    return false;
}

// VWAPAlgo Implementation
VWAPAlgo::VWAPAlgo(const Order &o) {
    parent_order = o;
    participation_rate = std::get<double>(o.algo_params);
    total_qty = o.quantity;
}

bool VWAPAlgo::tick(ExecutionEngine &engine) {
    if (filled_qty >= total_qty) {
        std::cout << "[ALGO] VWAP Completed for " << parent_order.market.ticker << std::endl;
        return true;
    }

    int64_t now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    
    // Throttle volume checks to avoid too frequent slicing
    if (last_slice_time_ns != 0 && (now_ns - last_slice_time_ns < 2e9)) {
        return false;
    }

    int64_t current_volume = engine.get_volume(parent_order.market);
    if (last_market_volume == -1) {
        last_market_volume = current_volume;
        last_slice_time_ns = now_ns;
        return false;
    }

    int64_t volume_delta = current_volume - last_market_volume;
    if (volume_delta <= 0) {
        last_market_volume = current_volume;
        return false;
    }

    int to_fill = static_cast<int>(volume_delta * participation_rate);
    to_fill = std::min(to_fill, total_qty - filled_qty);

    if (to_fill > 0) {
        Order slice = parent_order;
        slice.quantity = to_fill;
        slice.algo_type = AlgoType::None;
        if (slice.backend) {
            std::cout << "[ALGO] VWAP Slice: " << to_fill << " shares (Market Delta: " << volume_delta << ", Rate: " << participation_rate * 100 << "%)" << std::endl;
            slice.backend->create_order(slice);
        }
        filled_qty += to_fill;
        last_slice_time_ns = now_ns;
    }

    last_market_volume = current_volume;
    return false;
}

} // namespace bop
