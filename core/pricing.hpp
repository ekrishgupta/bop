#pragma once

#include "core.hpp"
#include <cmath>

namespace bop {

/**
 * @brief Greeks for prediction market contracts (Binary Options).
 */
struct Greeks {
    double delta;
    double gamma;
    double theta;
    double vega;
};

class PricingModel {
public:
    /**
     * @brief Calculate Greeks using a simplified Black-Scholes for binary options.
     * @param p Current market price (0.0 to 1.0)
     * @param sigma Annualized volatility
     * @param time_to_expiry Time in years
     */
    static Greeks calculate_greeks(Price price, double sigma, double time_to_expiry) {
        double p = price.to_double();
        if (p <= 0.0 || p >= 1.0 || time_to_expiry <= 0 || sigma <= 0) {
            return {0, 0, 0, 0};
        }

        // For a binary option (cash-or-nothing), the price is N(d2).
        // We can back out d2 from the current price.
        // This is a rough approximation for prediction markets.
        
        auto inv_normal_cdf = [](double p) {
            // Simple approximation of inverse normal CDF
            return std::sqrt(2.0) * std::erf(2.0 * p - 1.0); // Not exact, but illustrative
        };

        double d2 = inv_normal_cdf(p);
        double pdf_d2 = std::exp(-0.5 * d2 * d2) / std::sqrt(2.0 * M_PI);

        Greeks g;
        // Delta of binary option: e^-rT * pdf(d2) / (S * sigma * sqrt(T))
        // In our case, the "underlying" is usually not explicitly traded, but 
        // we can treat sensitivity to the probability.
        g.delta = pdf_d2 / (sigma * std::sqrt(time_to_expiry));
        
        // Gamma
        g.gamma = -d2 * pdf_d2 / (std::pow(sigma, 2) * time_to_expiry);
        
        // Theta (sensitivity to time decay)
        g.theta = -(pdf_d2 * d2) / (2.0 * time_to_expiry);
        
        // Vega (sensitivity to volatility)
        g.vega = -pdf_d2 * d2 / sigma;

        return g;
    }
};

// Pricing Models
struct MarketPrice {};

struct LimitPrice {
  Price price;
  explicit LimitPrice(Price p) : price(p) {}
};

struct Peg {
  ReferencePrice ref;
  Price offset;
  explicit Peg(ReferencePrice r, Price o) : ref(r), offset(o) {}
};

struct TrailingStop {
  Price trail_amount;
  explicit TrailingStop(Price t) : trail_amount(t) {}
};

// Order + LimitPrice -> Order
inline Order &operator+(Order &o, LimitPrice lp) {
  o.price = lp.price;
  return o;
}
inline Order &&operator+(Order &&o, LimitPrice lp) {
  o.price = lp.price;
  return std::move(o);
}

// Order + MarketPrice -> Order
inline Order &operator+(Order &o, MarketPrice) {
  o.price = Price(0);
  return o;
}
inline Order &&operator+(Order &&o, MarketPrice) {
  o.price = Price(0);
  return std::move(o);
}

// Order + Peg -> Order
inline Order &operator+(Order &o, Peg p) {
  o.price = Price(0);
  o.algo_type = AlgoType::Peg;
  o.algo_params = PegData{p.ref, p.offset};
  return o;
}
inline Order &&operator+(Order &&o, Peg p) {
  o.price = Price(0);
  o.algo_type = AlgoType::Peg;
  o.algo_params = PegData{p.ref, p.offset};
  return std::move(o);
}

// Order + TrailingStop -> Order
inline Order &operator+(Order &o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return o;
}
inline Order &&operator+(Order &&o, TrailingStop ts) {
  o.algo_type = AlgoType::Trailing;
  o.algo_params = ts.trail_amount.raw;
  return std::move(o);
}

} // namespace bop
