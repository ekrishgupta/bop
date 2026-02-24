#include "core/backtest.hpp"
#include "core/algo.hpp"
#include "core/market_base.hpp"
#include <iostream>
#include <vector>
#include <fstream>

using namespace bop;

// Define the global exchange reference for the DSL
BacktestExecutionEngine global_backtest_engine;
ExecutionEngine &LiveExchange = global_backtest_engine;

int main() {
    // 1. Setup Backtest Engine and Backend
    // Use the global engine we defined above
    BacktestExecutionEngine &engine = global_backtest_engine;
    auto backend = std::make_unique<BacktestMarketBackend>("BacktestExchange");
    
    // Configure models
    LatencyModel lm;
    lm.mean_latency_ns = 10000000; // 10ms
    lm.std_dev_ns = 2000000;      // 2ms
    backend->set_latency_model(lm);

    SlippageModel sm;
    sm.fixed_bps = 0.0;           // No fixed slippage for this demo
    sm.impact_constant = 0.000001; // minimal impact
    backend->set_slippage_model(sm);

    engine.register_backend(backend.get());

    // 2. Setup a strategy: Buy "AAPL" if price < 0.50
    std::cout << "[EXAMPLE] Setting up strategy: Buy AAPL if price < 0.50" << std::endl;
    
    auto cond_order = When(Market("AAPL", *backend).Price(YES) < 0.50_usd) >> (Buy(100) / Market("AAPL", *backend) / YES);
    cond_order.order.backend = backend.get();
    cond_order >> engine;

    // 3. Create some dummy historical data in a CSV file
    std::string csv_file = "backtest_data.csv";
    {
        std::ofstream file(csv_file);
        file << "timestamp,ticker,yes_price,no_price\n";
        file << "1600000000,AAPL,0.55,0.45\n";
        file << "1600000001,AAPL,0.52,0.48\n";
        file << "1600000002,AAPL,0.48,0.52\n"; // This should trigger the order
        file << "1600000003,AAPL,0.47,0.53\n"; // This should fill the order
        file << "1600000004,AAPL,0.50,0.50\n";
    }

    // 4. Run the backtest from CSV
    engine.run_from_csv(csv_file);

    // 5. Check results
    int64_t pos = engine.get_position(MarketId("AAPL"));
    std::cout << "[EXAMPLE] Final Position AAPL (from CSV): " << pos << std::endl;

    if (pos == 100) {
        std::cout << "[EXAMPLE] SUCCESS: CSV Order was filled correctly." << std::endl;
    } else {
        std::cout << "[EXAMPLE] FAILURE: CSV Order was not filled." << std::endl;
    }

    // 6. Test JSON support
    std::cout << "\n[EXAMPLE] Testing JSON backtest..." << std::endl;
    
    // Set initial price high to prevent immediate trigger
    engine.update_market("TSLA", 1.0_usd, 0.0_usd);

    std::string json_file = "backtest_data.json";
    {
        std::ofstream file(json_file);
        file << "[\n";
        file << "  {\"timestamp\": 1700000000, \"ticker\": \"TSLA\", \"yes_price\": 0.80, \"no_price\": 0.20},\n";
        file << "  {\"timestamp\": 1700000001, \"ticker\": \"TSLA\", \"yes_price\": 0.65, \"no_price\": 0.35},\n";
        file << "  {\"timestamp\": 1700000002, \"ticker\": \"TSLA\", \"yes_price\": 0.65, \"no_price\": 0.35}\n";
        file << "]\n";
    }

    // Setup TSLA strategy: Buy if TSLA < 0.75
    auto tsla_order = When(Market("TSLA", *backend).Price(YES) < 0.75_usd) >> (Buy(50) / Market("TSLA", *backend) / YES);
    tsla_order.order.backend = backend.get();
    tsla_order >> engine;

    engine.run_from_json(json_file);

    int64_t tsla_pos = engine.get_position(MarketId("TSLA"));
    std::cout << "[EXAMPLE] Final Position TSLA (from JSON): " << tsla_pos << std::endl;

    engine.report();

    if (tsla_pos == 50) {
        std::cout << "[EXAMPLE] SUCCESS: JSON Order was filled correctly." << std::endl;
    } else {
        std::cout << "[EXAMPLE] FAILURE: JSON Order was not filled." << std::endl;
    }

    return 0;
}
