#pragma once

#include "engine.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace bop {

/**
 * @brief An Execution Engine that provides a real-time CLI dashboard.
 */
class DashboardExecutionEngine : public LiveExecutionEngine {
    std::thread dashboard_thread;
    std::atomic<bool> show_dashboard{true};

public:
    DashboardExecutionEngine() : LiveExecutionEngine() {}
    
    ~DashboardExecutionEngine() {
        show_dashboard = false;
        if (dashboard_thread.joinable()) dashboard_thread.join();
    }

    void run() override {
        dashboard_thread = std::thread([this]() {
            while (show_dashboard && is_running) {
                render_dashboard();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        LiveExecutionEngine::run();
    }

private:
    void render_dashboard() {
        // Clear screen (ANSI escape code)
        std::cout << "\033[2J\033[1;1H";
        
        std::cout << "================================================================
";
        std::cout << "   BOP TRADING ENGINE DASHBOARD | Status: " << (is_running ? "RUNNING" : "STOPPED") << "
";
        std::cout << "================================================================
";
        
        Price balance = get_balance();
        Price exposure = get_exposure();
        Price pnl = get_pnl();
        int64_t daily_pnl = current_daily_pnl_raw.load();
        
        std::cout << std::left << std::setw(20) << "Total Balance:" << balance << "
";
        std::cout << std::left << std::setw(20) << "Net Exposure:" << exposure << "
";
        std::cout << std::left << std::setw(20) << "Realized PnL:" << pnl << "
";
        std::cout << std::left << std::setw(20) << "Daily PnL:" << Price(daily_pnl) << " / " << limits.daily_loss_limit << "
";
        
        // Latency
        int64_t last_tick = last_tick_time_ns.load();
        if (last_tick > 0) {
            int64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            double latency_ms = (now - last_tick) / 1e6;
            std::cout << std::left << std::setw(20) << "Engine Latency:" << std::fixed << std::setprecision(2) << latency_ms << " ms
";
        }

        std::cout << "----------------------------------------------------------------
";
        std::cout << " ACTIVE ALGORITHMS & STRATEGIES (" << GlobalAlgoManager.active_count() << ")
";
        std::cout << "----------------------------------------------------------------
";
        // Manager doesn't expose list of names yet, but we show the count.
        
        std::cout << "----------------------------------------------------------------
";
        std::cout << " OPEN ORDERS
";
        std::cout << "----------------------------------------------------------------
";
        auto orders = order_store.get_all();
        int open_count = 0;
        for (const auto& ord : orders) {
            if (ord.status == OrderStatus::Open || ord.status == OrderStatus::PartiallyFilled) {
                std::cout << " ID: " << std::setw(15) << ord.id 
                          << " | " << (ord.order.is_buy ? "BUY" : "SEL") 
                          << " | " << std::setw(10) << ord.order.market.ticker
                          << " | " << ord.order.quantity << " @ " << ord.order.price << "
";
                if (++open_count >= 5) {
                    std::cout << " ... and more
";
                    break;
                }
            }
        }
        if (open_count == 0) std::cout << " No open orders.
";

        std::cout << "----------------------------------------------------------------
";
        std::cout << " RECENT FILLS
";
        std::cout << "----------------------------------------------------------------
";
        int fill_count = 0;
        for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
            if (!it->fills.empty()) {
                for (const auto& fill : it->fills) {
                    std::cout << " TKR: " << std::setw(10) << it->order.market.ticker
                              << " | QTY: " << std::setw(6) << fill.quantity 
                              << " | PRC: " << fill.price << "
";
                    if (++fill_count >= 5) break;
                }
            }
            if (fill_count >= 5) break;
        }
        if (fill_count == 0) std::cout << " No recent fills.
";

        std::cout << "================================================================
";
        std::cout << std::endl;
    }
};

} // namespace bop
