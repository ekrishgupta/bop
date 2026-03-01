#pragma once

#include <type_traits>

namespace bop {

// Capability Tags
struct SupportsPostOnly {};
struct SupportsIceberg {};
struct SupportsSTP {};
struct SupportsFOK {};
struct SupportsIOC {};

// Traits base
template <typename T> struct BackendTraits {
  static constexpr bool supports_post_only = false;
  static constexpr bool supports_iceberg = false;
  static constexpr bool supports_stp = false;
  static constexpr bool supports_fok = true; // Default
  static constexpr bool supports_ioc = true; // Default
};

// Concepts (C++20)
template <typename T>
concept PostOnlyCapable = BackendTraits<T>::supports_post_only;

template <typename T>
concept IcebergCapable = BackendTraits<T>::supports_iceberg;

template <typename T>
concept STPCapable = BackendTraits<T>::supports_stp;

template <typename T>
concept FOKCapable = BackendTraits<T>::supports_fok;

template <typename T>
concept IOCCapable = BackendTraits<T>::supports_ioc;

} // namespace bop
