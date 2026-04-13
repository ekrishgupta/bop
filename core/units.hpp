#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace bop {

/**
 * @brief Template for type-safe units.
 * Prevents mixing different units (e.g., Shares and USD) at compile time.
 */
template <typename Tag, typename T = int64_t> struct Quantity {
  T raw;

  constexpr Quantity() : raw(0) {}
  constexpr explicit Quantity(T r) : raw(r) {}

  template <typename OtherTag>
  Quantity(const Quantity<OtherTag, T> &) = delete; // Block implicit conversion

  // Arithmetic (same unit only)
  constexpr Quantity operator+(Quantity other) const {
    return Quantity(raw + other.raw);
  }
  constexpr Quantity operator-(Quantity other) const {
    return Quantity(raw - other.raw);
  }
  constexpr Quantity &operator+=(Quantity other) {
    raw += other.raw;
    return *this;
  }
  constexpr Quantity &operator-=(Quantity other) {
    raw -= other.raw;
    return *this;
  }

  // Scalar multiplication/division
  constexpr Quantity operator*(T scalar) const {
    return Quantity(raw * scalar);
  }
  constexpr Quantity operator/(T scalar) const {
    return Quantity(raw / scalar);
  }

  // Comparisons (same unit only)
  constexpr bool operator>(Quantity other) const { return raw > other.raw; }
  constexpr bool operator<(Quantity other) const { return raw < other.raw; }
  constexpr bool operator>=(Quantity other) const { return raw >= other.raw; }
  constexpr bool operator<=(Quantity other) const { return raw <= other.raw; }
  constexpr bool operator==(Quantity other) const { return raw == other.raw; }
  constexpr bool operator!=(Quantity other) const { return raw != other.raw; }

  constexpr Quantity operator-() const { return Quantity(-raw); }

  constexpr double to_double() const { return static_cast<double>(raw); }

  friend std::ostream &operator<<(std::ostream &os, const Quantity &q) {
    os << q.raw;
    return os;
  }
};

// Tags for different units
struct SharesTag {};
struct USDTag {};
struct TicksTag {};

using Shares = Quantity<SharesTag, int64_t>;
using Units = Shares;
using Ticks = Quantity<TicksTag, int64_t>;

} // namespace bop
