// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file variance.hpp
 * @brief Variance / range selection for stat values.
 *
 * Data records (creatures, archetypes, items) may express a value either as a
 * plain integer, a dice expression string such as @c "2d6", or an explicit
 * range object @c {"min": ..., "max": ...}. When a caller selects an entry
 * (e.g. an animal as a fight opponent) it may request a "variance": weakest
 * (minimum), weak (lower third, random), average (middle third, random),
 * strong (upper third, random), strongest (maximum), or random (default:
 * uniform over the range, or a true dice roll for dice expressions).
 *
 * @par Why is this in the shared core?
 * A monster's hit points written as @c "2d6" must resolve the same way whether
 * it is created through the universal engine or the generated character
 * classes. Range selection is therefore part of the shared core, and both
 * modes call @c readVariantValue with the same variance policy — which is
 * what lets a Monte-Carlo run (average variance) and a "roll it for real"
 * run (random variance) both stay consistent across modes.
 *
 * @par Why preserve ranges instead of collapsing them?
 * A source book value like @c "2d6+4" carries information (its distribution,
 * its bounds) that a single rolled number loses. The ruleset keeps the range
 * verbatim, and this header is the single place that turns a range into a
 * concrete value — so the choice of variance is made by the caller, at the
 * moment of use, not baked in at data-entry time.
 */
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/concepts.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <utility>

namespace rpg_os {

/// How a ranged value is picked from its [min, max] interval.
///
/// @par Why thirds for the middle bands?
/// The non-extreme bands (weak/average/strong) divide the range into thirds so
/// that "average" never overlaps "weak" or "strong" and the bands are
/// meaningful regardless of how wide the original range was — a consistent,
/// game-system-independent convention both modes share.
enum class Variance {
  Random,   ///< uniform over the full range (dice expressions are rolled)
  Weakest,  ///< the minimum
  Weak,     ///< uniformly in the lower third
  Average,  ///< uniformly in the middle third
  Strong,   ///< uniformly in the upper third
  Strongest ///< the maximum
};

/// Picks an integer in [min, max] according to `variance`.
///
/// @par Why a degenerate range returns `min`?
/// When @c min >= @c max there is only one sensible value; returning it (for
/// both the empty and reversed case) keeps the function total and avoids an
/// out-of-contract RNG call. This mirrors how a ruleset's plain-integer values
/// behave: no range, no variance.
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
///
/// @par Why not just roll to find bounds?
/// Rolling would give a different answer every time. Instead the bounds are
/// derived arithmetically from the die specs (each die contributes 1..sides,
/// or the reversed interval when subtracted), so the same expression always
/// reports the same range and the third-band selection is deterministic
/// across modes.
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
///
/// @par Why return 0 for non-numeric entries?
/// Data records occasionally carry non-numeric fields in the same bag
/// (e.g. a name). Treating those as "no numeric value" keeps the single
/// entry point total — callers never need a separate path for "this entry is
/// not a number", and 0 is the same neutral default @ref NullStatProvider
/// uses for missing stats.
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
