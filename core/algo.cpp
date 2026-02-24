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

bool TWAPAlgo::tick_impl(ExecutionEngine &engine) {
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

bool TrailingStopAlgo::tick_impl(ExecutionEngine &engine) {
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

bool PegAlgo::tick_impl(ExecutionEngine &engine) {
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

bool VWAPAlgo::tick_impl(ExecutionEngine &engine) {
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

// ArbitrageAlgo Implementation
ArbitrageAlgo::ArbitrageAlgo(MarketId m1, const MarketBackend *b1, MarketId m2,
                             const MarketBackend *b2, Price min_profit, int qty)
    : m1(m1), m2(m2), b1(b1), b2(b2), min_profit(min_profit), quantity(qty) {}

bool ArbitrageAlgo::tick_impl(ExecutionEngine &engine) {
    if (!active) return true;

    // We check prices on both backends
    Price p1_yes = b1->get_price(m1, true);
    Price p2_yes = b2->get_price(m2, true);

    if (p1_yes.raw == 0 || p2_yes.raw == 0) return false;

    // Case 1: Buy on b1, sell on b2
    // Profit = Price(b2) - Price(b1)
    if (p2_yes > (p1_yes + min_profit)) {
        std::cout << "[ALGO] ARB OPPORTUNITY: Buy " << b1->name() << " @ " << p1_yes 
                  << ", Sell " << b2->name() << " @ " << p2_yes << std::endl;
        
        Order buy_o(m1, quantity, true, true, p1_yes, 0);
        buy_o.backend = b1;
        
        Order sell_o(m2, quantity, false, true, p2_yes, 0);
        sell_o.backend = b2;

        buy_o >> engine;
        sell_o >> engine;
        
        active = false; // Execute once for this instance
        return true;
    }

    // Case 2: Buy on b2, sell on b1
    if (p1_yes > (p2_yes + min_profit)) {
        std::cout << "[ALGO] ARB OPPORTUNITY: Buy " << b2->name() << " @ " << p2_yes 
                  << ", Sell " << b1->name() << " @ " << p1_yes << std::endl;
        
        Order buy_o(m2, quantity, true, true, p2_yes, 0);
        buy_o.backend = b2;
        
        Order sell_o(m1, quantity, false, true, p1_yes, 0);
        sell_o.backend = b1;

        buy_o >> engine;
        sell_o >> engine;
        
        active = false;
        return true;
    }

    return false;
}

// MarketMakerAlgo Implementation
MarketMakerAlgo::MarketMakerAlgo(const Order &o) {
    parent_order = o;
    auto data = std::get<MarketMakerData>(o.algo_params);
    spread = data.spread;
    ref = data.ref;
}

bool MarketMakerAlgo::tick_impl(ExecutionEngine &engine) {
    Price ref_price;
    if (ref == ReferencePrice::Mid) {
        Price bid = engine.get_depth(parent_order.market, true);
        Price ask = engine.get_depth(parent_order.market, false);
        if (bid.raw == 0 || ask.raw == 0) return false;
        ref_price = Price((bid.raw + ask.raw) / 2);
    } else {
        bool is_bid = (ref == ReferencePrice::Bid);
        ref_price = engine.get_depth(parent_order.market, is_bid);
    }

    if (ref_price.raw == 0) return false;

    // Check if one side was filled
    if (!bid_id.empty() || !ask_id.empty()) {
        auto records = engine.get_orders();
        for (const auto& r : records) {
            if (r.id == bid_id && r.status == OrderStatus::Filled) {
                std::cout << "[ALGO] MarketMaker: Bid side filled. Cancelling ask." << std::endl;
                if (!ask_id.empty() && parent_order.backend) parent_order.backend->cancel_order(ask_id);
                bid_id = ""; ask_id = "";
                return true; 
            }
            if (r.id == ask_id && r.status == OrderStatus::Filled) {
                std::cout << "[ALGO] MarketMaker: Ask side filled. Cancelling bid." << std::endl;
                if (!bid_id.empty() && parent_order.backend) parent_order.backend->cancel_order(bid_id);
                bid_id = ""; ask_id = "";
                return true;
            }
        }
    }

    Price target_bid = ref_price - Price(spread.raw / 2);
    Price target_ask = ref_price + Price(spread.raw / 2);

    if (ref_price != last_ref_price) {
        if (!bid_id.empty() && parent_order.backend) parent_order.backend->cancel_order(bid_id);
        if (!ask_id.empty() && parent_order.backend) parent_order.backend->cancel_order(ask_id);

        Order bid = parent_order;
        bid.is_buy = true;
        bid.price = target_bid;
        bid.algo_type = AlgoType::None;

        Order ask = parent_order;
        ask.is_buy = false;
        ask.price = target_ask;
        ask.algo_type = AlgoType::None;

        if (parent_order.backend) {
            std::cout << "[ALGO] MarketMaker: Quoting " << parent_order.market.ticker 
                      << " Bid: " << target_bid << " Ask: " << target_ask << std::endl;
            bid_id = parent_order.backend->create_order(bid);
            ask_id = parent_order.backend->create_order(ask);
        }
        last_ref_price = ref_price;
    }
    return false;
}

} // namespace bop
