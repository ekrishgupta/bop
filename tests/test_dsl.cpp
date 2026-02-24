#include "bop.hpp"
#include <iostream>
#include <cassert>
#include <string>

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " << msg << std::endl; \
        exit(1); \
    }

void test_basic_order_construction() {
    std::cout << "Testing basic order construction..." << std::endl;
    
    auto o = Buy(100_shares) / "AAPL"_mkt / YES;
    
    TEST_ASSERT(o.quantity == 100, "Quantity should be 100");
    TEST_ASSERT(o.is_buy == true, "Side should be Buy");
    TEST_ASSERT(o.outcome_yes == true, "Outcome should be YES");
    TEST_ASSERT(o.market.ticker == "AAPL", "Ticker should be AAPL");
    TEST_ASSERT(o.market.hash == bop::fnv1a("AAPL"), "Market hash mismatch");
    
    auto s = Sell(50_shares) / "TSLA"_mkt / NO;
    TEST_ASSERT(s.quantity == 50, "Quantity should be 50");
    TEST_ASSERT(s.is_buy == false, "Side should be Sell");
    TEST_ASSERT(s.outcome_yes == false, "Outcome should be NO");
    TEST_ASSERT(s.market.ticker == "TSLA", "Ticker should be TSLA");
}

void test_modifiers() {
    std::cout << "Testing DSL modifiers..." << std::endl;
    
    auto o = Buy(100_shares) / "BTC"_mkt / YES + LimitPrice(0.65_usd) | IOC | PostOnly;
    
    TEST_ASSERT(o.price.raw == Price::from_usd(0.65).raw, "Limit price mismatch");
    TEST_ASSERT(o.tif == TimeInForce::IOC, "TIF should be IOC");
    TEST_ASSERT(o.post_only == true, "PostOnly should be true");
    
    auto o2 = Sell(200_shares) / "ETH"_mkt / NO + MarketPrice() | FOK | Iceberg(50_shares);
    TEST_ASSERT(o2.price.raw == 0, "Market price raw should be 0");
    TEST_ASSERT(o2.tif == TimeInForce::FOK, "TIF should be FOK");
    TEST_ASSERT(o2.display_qty == 50, "Iceberg display quantity mismatch");
}

void test_brackets() {
    std::cout << "Testing bracket orders..." << std::endl;
    
    auto o = (Buy(100_shares) / "AAPL"_mkt / YES + LimitPrice(0.50_usd)) 
             & TakeProfit(0.75_usd) & StopLoss(0.40_usd);
             
    TEST_ASSERT(o.tp_price.raw == Price::from_usd(0.75).raw, "TakeProfit mismatch");
    TEST_ASSERT(o.sl_price.raw == Price::from_usd(0.40).raw, "StopLoss mismatch");
}

void test_conditions() {
    std::cout << "Testing conditions..." << std::endl;
    
    auto c = Market("BTC").Price(YES) > 0.60_usd;
    TEST_ASSERT(c.threshold == Price::from_usd(0.60).raw, "Condition threshold mismatch");
    TEST_ASSERT(c.is_greater == true, "Condition operator mismatch");
    
    auto c2 = Position("AAPL"_mkt) < 500;
    TEST_ASSERT(c2.threshold == 500, "Position threshold mismatch");
    TEST_ASSERT(c2.is_greater == false, "Position operator mismatch");
    
    auto c3 = Balance() > 1000;
    TEST_ASSERT(c3.threshold == 1000, "Balance threshold mismatch");
}

void test_composition() {
    std::cout << "Testing condition composition..." << std::endl;
    
    auto cond = (Market("BTC").Price(YES) > 0.60_usd) && (Position("BTC"_mkt) < 100);
    
    // Check if we can build a conditional order with it
    auto co = When(cond) >> (Buy(10_shares) / "BTC" / YES);
    
    TEST_ASSERT(co.order.quantity == 10, "Conditional order quantity mismatch");
}

int main() {
    std::cout << "Starting DSL Unit Tests..." << std::endl;
    
    test_basic_order_construction();
    test_modifiers();
    test_brackets();
    test_conditions();
    test_composition();
    
    std::cout << "All DSL Unit Tests Passed!" << std::endl;
    return 0;
}
