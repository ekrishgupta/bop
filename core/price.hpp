#pragma once

#include <cstdint>

#include <iostream>

namespace bop {

/**
 * @brief Standardized Price class using fixed-point arithmetic.
 * SCALE = 1,000,000 provides 6 decimal places of precision,
 * which is sufficient for most prediction markets (cents = 2, poly = 4).
 */
struct Price {
  int64_t raw;
  static constexpr int64_t SCALE = 1000000;

  constexpr Price() : raw(0) {}
  constexpr explicit Price(int64_t r) : raw(r) {}

  static constexpr Price from_double(double d) {
    return Price(static_cast<int64_t>(d * SCALE + (d >= 0 ? 0.5 : -0.5)));
  }

  static constexpr Price from_ticks(int64_t ticks, int64_t ticks_per_unit) {
    // Ensure we don't divide by zero and handle scale differences
    return Price(ticks * (SCALE / ticks_per_unit));
  }

  static constexpr Price from_usd(double d) { return from_double(d); }
  static constexpr Price from_cents(int64_t cents) {
    return from_ticks(cents, 100);
  }

  constexpr double to_double() const {
    return static_cast<double>(raw) / SCALE;
  }

  constexpr int64_t to_ticks(int64_t ticks_per_unit) const {
    return raw / (SCALE / ticks_per_unit);
  }

  // Arithmetic
  constexpr Price operator+(Price other) const {
    return Price(raw + other.raw);
  }
  constexpr Price operator-(Price other) const {
    return Price(raw - other.raw);
  }

  constexpr Price operator-() const { return Price(-raw); }

  // Comparisons
  constexpr bool operator>(Price other) const { return raw > other.raw; }
  constexpr bool operator<(Price other) const { return raw < other.raw; }
  constexpr bool operator>=(Price other) const { return raw >= other.raw; }
  constexpr bool operator<=(Price other) const { return raw <= other.raw; }
  constexpr bool operator==(Price other) const { return raw == other.raw; }
  constexpr bool operator!=(Price other) const { return raw != other.raw; }

  friend std::ostream &operator<<(std::ostream &os, const Price &p) {
    os << p.to_double();
    return os;
  }
};

} // namespace bop
