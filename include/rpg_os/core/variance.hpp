// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Variance / range selection.
//
// Data records (creatures, archetypes, items) may express a value either as a
// plain integer, a dice expression string such as "2d6", or an explicit range
// object {"min": ..., "max": ...}. When a caller selects an entry (e.g. an
// animal as a fight opponent) it may request a "variance": weakest (minimum),
// weak (lower third, random), average (middle third, random), strong (upper
// third, random), strongest (maximum), or random (default: uniform over the
// range, or a true dice roll for dice expressions).
//
// Shared by the universal engine and the generated specific-mode code.
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/concepts.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <utility>

namespace rpg_os {

/// How a ranged value is picked from its [min, max] interval.
enum class Variance {
  Random,   ///< uniform over the full range (dice expressions are rolled)
  Weakest,  ///< the minimum
  Weak,     ///< uniformly in the lower third
  Average,  ///< uniformly in the middle third
  Strong,   ///< uniformly in the upper third
  Strongest ///< the maximum
};

/// Picks an integer in [min, max] according to `variance`.
template <RandomNumberGenerator Rng>
[[nodiscard]] inline int32_t pickVariant(int32_t min, int32_t max, Variance variance, Rng &rng) {
  if (min >= max) {
    return min;
  }
  const int32_t span = max - min;
  switch (variance) {
  case Variance::Weakest:
    return min;
  case Variance::Strongest:
    return max;
  case Variance::Weak: {
    const int32_t hi = min + span / 3;
    return min + rng(0, hi - min);
  }
  case Variance::Average: {
    const int32_t lo = min + span / 3;
    const int32_t hi = min + 2 * span / 3;
    return lo + rng(0, hi - lo);
  }
  case Variance::Strong: {
    const int32_t lo = min + 2 * span / 3;
    return lo + rng(0, max - lo);
  }
  case Variance::Random:
  default:
    return min + rng(0, span);
  }
}

/// Computes the [min, max] bounds of a dice expression (for range selection).
[[nodiscard]] inline std::pair<int32_t, int32_t> diceBounds(const DiceExpression &expression) {
  int32_t lo = expression.constant();
  int32_t hi = expression.constant();
  for (const DieSpec &die : expression.dice()) {
    if (die.sign > 0) {
      lo += die.count * 1;
      hi += die.count * die.sides;
    } else {
      lo += die.count * die.sides * die.sign;
      hi += die.count * 1 * die.sign;
    }
  }
  return {lo, hi};
}

/// Reads a stat value from a JSON data entry, honouring ranges and variance.
/// Supported forms:
///   - integer            -> that value (no range),
///   - string dice expr   -> rolled for Variance::Random, otherwise a value in
///                           the dice's [min, max] per the variance,
///   - {"min","max"}      -> a value in [min, max] per the variance.
template <RandomNumberGenerator Rng>
[[nodiscard]] inline int32_t readVariantValue(const Json &entry, Variance variance, Rng &rng) {
  if (entry.is_number_integer()) {
    return entry.get<int32_t>();
  }
  if (entry.is_string()) {
    const DiceExpression expression(entry.get<std::string>());
    if (variance == Variance::Random) {
      return expression.rollSum(rng);
    }
    const auto [lo, hi] = diceBounds(expression);
    return pickVariant(lo, hi, variance, rng);
  }
  if (entry.is_object() && entry.contains("min") && entry.contains("max")) {
    const int32_t lo = entry.at("min").get<int32_t>();
    const int32_t hi = entry.at("max").get<int32_t>();
    return pickVariant(lo, hi, variance, rng);
  }
  // Non-numeric entries (e.g. a name) are treated as "no numeric value".
  return 0;
}

} // namespace rpg_os
