#include "core/engine.hpp"
#include "core/logic.hpp"
#include "core/market_base.hpp"
#include <iostream>

using namespace bop;

// Mock Backend for demonstration
struct MockBackend : public MarketBackend {
    std::string name_str;
    Price current_price;
    MockBackend(std::string name) : name_str(name), current_price(0.60_usd) {}
    std::string name() const override { return name_str; }
    Price get_price(MarketId market, bool outcome_yes) const override {
        return current_price;
    }
    Price get_depth(MarketId market, bool is_bid) const override { return current_price; }
    std::string create_order(const Order& o) const override { 
        std::cout << "[" << name_str << "] Order created for " << o.market.ticker << std::endl;
        return "mock_id"; 
    }
};

// Define global exchange reference
ExecutionEngine global_engine;
ExecutionEngine &bop::LiveExchange = global_engine;

int main() {
    MockBackend poly("Polymarket");
    MockBackend kalshi("Kalshi");

    global_engine.register_backend(&poly);
    global_engine.register_backend(&kalshi);

    // 1. Register super-ticker "BTC" across multiple exchanges
    std::cout << "[INFO] Registering 'BTC' super-ticker on Polymarket and Kalshi..." << std::endl;
    MarketRegistry::Register("BTC", MarketId("BTC_CONTRACT_POLY"), poly);
    MarketRegistry::Register("BTC", MarketId("BTC_CONTRACT_KALSHI"), kalshi);

    // 2. Set different prices on each exchange
    poly.current_price = 0.55_usd;
    kalshi.current_price = 0.45_usd;

    // 3. Query universal price (should find the best/lowest price for YES)
    Price best_btc = global_engine.get_universal_price(MarketId("BTC"), true);
    std::cout << "[INFO] Best Universal Price for BTC: " << best_btc << std::endl;

    // 4. Use the DSL with UniversalMarket
    // This will automatically check across all registered backends for "BTC"
    auto condition = When(UniversalMarket("BTC").Price(YES) < 0.50_usd);
    
    if (condition.condition.eval()) {
        std::cout << "[SUCCESS] Condition Met: BTC is available under 0.50 on at least one registered exchange." << std::endl;
    } else {
        std::cout << "[FAILURE] Condition NOT met." << std::endl;
    }

    // 5. Test price update - move both above 0.50
    std::cout << "[INFO] Updating prices to be above 0.50..." << std::endl;
    poly.current_price = 0.60_usd;
    kalshi.current_price = 0.60_usd;

    if (condition.condition.eval()) {
        std::cout << "[FAILURE] Condition still met but should not be." << std::endl;
    } else {
        std::cout << "[SUCCESS] Condition correctly NOT met when all exchange prices are high." << std::endl;
    }

    return 0;
}
