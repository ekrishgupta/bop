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
        
        std::cout << "================================================================\n";
        std::cout << "   BOP TRADING ENGINE DASHBOARD | Status: " << (is_running ? "RUNNING" : "STOPPED") << "\n";
        std::cout << "================================================================\n";
        
        Price balance = get_balance();
        Price exposure = get_exposure();
        Price pnl = get_pnl();
        int64_t daily_pnl = current_daily_pnl_raw.load();
        
        std::cout << std::left << std::setw(20) << "Total Balance:" << balance << "\n";
        std::cout << std::left << std::setw(20) << "Net Exposure:" << exposure << "\n";
        std::cout << std::left << std::setw(20) << "Realized PnL:" << pnl << "\n";
        std::cout << std::left << std::setw(20) << "Daily PnL:" << Price(daily_pnl) << " / " << limits.daily_loss_limit << "\n";
        
        // Portfolio Greeks
        PortfolioGreeks pg;
        std::unordered_map<uint32_t, double> volatilities;
        for (auto const& [hash, tracker] : market_volatility) {
            volatilities[hash] = tracker.current_vol;
        }
        pg = const_cast<GreekEngine&>(greek_engine).calculate_portfolio_greeks(get_positions_map(), backends_, volatilities);
        
        std::cout << "Greeks: Delta=" << std::fixed << std::setprecision(2) << pg.total_delta 
                  << " | Gamma=" << pg.total_gamma 
                  << " | Theta=" << pg.total_theta 
                  << " | Vega=" << pg.total_vega << "\n";

        // Latency
        int64_t last_tick = last_tick_time_ns.load();
        if (last_tick > 0) {
            int64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            double latency_ms = (now - last_tick) / 1e6;
            std::cout << std::left << std::setw(20) << "Engine Latency:" << std::fixed << std::setprecision(2) << latency_ms << " ms\n";
        }

        std::cout << "----------------------------------------------------------------\n";
        std::cout << " ACTIVE ALGORITHMS & STRATEGIES (" << GlobalAlgoManager.active_count() << ")\n";
        std::cout << "----------------------------------------------------------------\n";
        
        std::cout << "----------------------------------------------------------------\n";
        std::cout << " OPEN ORDERS\n";
        std::cout << "----------------------------------------------------------------\n";
        auto orders = order_store.get_all();
        int open_count = 0;
        for (const auto& ord : orders) {
            if (ord.status == OrderStatus::Open || ord.status == OrderStatus::PartiallyFilled) {
                std::cout << " ID: " << std::setw(15) << ord.id 
                          << " | " << (ord.order.is_buy ? "BUY" : "SEL") 
                          << " | " << std::setw(10) << ord.order.market.ticker
                          << " | " << ord.order.quantity << " @ " << ord.order.price << "\n";
                if (++open_count >= 5) {
                    std::cout << " ... and more\n";
                    break;
                }
            }
        }
        if (open_count == 0) std::cout << " No open orders.\n";

        std::cout << "----------------------------------------------------------------\n";
        std::cout << " RECENT FILLS\n";
        std::cout << "----------------------------------------------------------------\n";
        int fill_count = 0;
        for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
            if (!it->fills.empty()) {
                for (const auto& fill : it->fills) {
                    std::cout << " TKR: " << std::setw(10) << it->order.market.ticker
                              << " | QTY: " << std::setw(6) << fill.quantity 
                              << " | PRC: " << fill.price << "\n";
                    if (++fill_count >= 5) break;
                }
            }
            if (fill_count >= 5) break;
        }
        if (fill_count == 0) std::cout << " No recent fills.\n";

        std::cout << "================================================================\n";
        std::cout << std::endl;
    }
};

} // namespace bop
