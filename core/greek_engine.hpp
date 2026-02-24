#pragma once

#include "core.hpp"
#include "market_base.hpp"
#include "pricing.hpp"
#include <unordered_map>
#include <vector>
#include <chrono>

namespace bop {

struct PortfolioGreeks {
    double total_delta = 0;
    double total_gamma = 0;
    double total_theta = 0;
    double total_vega = 0;
};

class GreekEngine {
public:
    PortfolioGreeks calculate_portfolio_greeks(
        const std::unordered_map<uint32_t, int64_t>& positions,
        const std::vector<const MarketBackend*>& backends,
        const std::unordered_map<uint32_t, double>& volatilities
    ) {
        PortfolioGreeks pg;
        auto now = std::chrono::system_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        for (auto const& [hash, qty] : positions) {
            if (qty == 0) continue;

            for (auto b : backends) {
                MarketId m(hash);
                Price p = b->get_price(m, true);
                if (p.raw == 0) continue;

                int64_t expiry = b->get_market_expiry(m);
                if (expiry == 0) expiry = now_ms + (30LL * 24 * 3600 * 1000); // Default 30 days

                double t = (double)(expiry - now_ms) / (365.0 * 24 * 3600 * 1000);
                if (t <= 0) t = 0.0001;

                double sigma = 0.20; // Default 20% vol
                auto it_vol = volatilities.find(hash);
                if (it_vol != volatilities.end()) sigma = it_vol->second;

                Greeks g = PricingModel::calculate_greeks(p, sigma, t);
                
                pg.total_delta += g.delta * qty;
                pg.total_gamma += g.gamma * qty;
                pg.total_theta += g.theta * qty;
                pg.total_vega += g.vega * qty;
                
                break;
            }
        }
        return pg;
    }
};

} // namespace bop
