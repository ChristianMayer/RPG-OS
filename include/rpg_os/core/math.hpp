// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file math.hpp
 * @ingroup rpg_os_core
 * @brief Shared math helpers for formula evaluation and stat arithmetic.
 *
 * These functions are the single source of truth for the formula function set
 * (@c min @c max @c floor @c ceil @c round @c clamp). Both the universal AST
 * evaluator (see @c expression.hpp) and the generated specific-mode compiled
 * formulas call into this header, so both modes agree on the exact semantics
 * of every function a ruleset formula may use.
 *
 * @par Why a dedicated namespace?
 * The names @c min, @c max, and @c clamp collide with @c std::min, @c std::max
 * and @c std::clamp, and with the macros of the same name on Windows. Wrapping
 * them in @c rpg_os::math keeps the intent unambiguous and lets generated code
 * call these helpers without dragging @c std into scope or relying on ADL.
 *
 * @par Why wrappers instead of using @c std directly in formulas?
 * The generated code compiles the *same* formula grammar to C++. If the
 * evaluator called @c std::round while generated code called something else,
 * a formula could produce different results depending on the mode. Routing
 * every formula function through this header pins one implementation for
 * both paths — and leaves a single place to adjust if a game system ever
 * needs a different rounding convention.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rpg_os {
namespace math {

namespace detail {
/// Truncates toward zero (matches std::trunc) for any finite value that fits
/// in an int64. The shared building block for the floor/ceil/round helpers
/// below; the explicit casts keep this clean under -Wconversion.
[[nodiscard]] constexpr double trunc(double value) noexcept {
  return static_cast<double>(static_cast<int64_t>(value));
}
} // namespace detail

/// Floors a value (matches std::floor).
///
/// @par Why not just return std::floor?
/// C++23 makes @c std::floor @c constexpr, but older libstdc++ (GCC < 13, e.g.
/// the clang toolchain on the CI runner) has not implemented that, so a
/// @c static_assert on this helper would not compile there. This manual form is
/// @c constexpr on every toolchain and matches @c std::floor exactly for all
/// finite values in the int64 range.
[[nodiscard]] constexpr double floor(double value) noexcept {
  const double t = detail::trunc(value);
  return t > value ? t - 1.0 : t;
}

/// Ceils a value (matches std::ceil). See @ref floor for why it does not
/// delegate to @c std::ceil.
[[nodiscard]] constexpr double ceil(double value) noexcept {
  const double t = detail::trunc(value);
  return t < value ? t + 1.0 : t;
}

/// Rounds half away from zero (matches std::round). Note this is *not* the
/// same as "round half up" for negative values; formulas must be written with
/// this convention in mind. See @ref floor for why it does not delegate to
/// @c std::round.
[[nodiscard]] constexpr double round(double value) noexcept {
  const double t = detail::trunc(value);
  const double diff = value - t;
  if (diff >= 0.5) {
    return t + 1.0;
  }
  if (diff <= -0.5) {
    return t - 1.0;
  }
  return t;
}

/// Minimum of two values (matches std::min).
[[nodiscard]] constexpr double min(double a, double b) noexcept {
  return std::min(a, b);
}

/// Maximum of two values (matches std::max).
[[nodiscard]] constexpr double max(double a, double b) noexcept {
  return std::max(a, b);
}

/// Clamps `value` into the inclusive range [lo, hi].
///
/// @par Why not std::clamp?
/// @c std::clamp returns a @c const double& to one of its arguments, which
/// forces callers to keep temporaries alive; a by-value helper has no such
/// lifetime subtlety and is trivially inlined. Hand-rolling the comparison
/// also avoids the @c NaN pitfall being a silent pass-through (NaN compares
/// false and thus returns @c lo, matching the clamp semantics rulesets want).
[[nodiscard]] constexpr double clamp(double value, double lo, double hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// Integer clamp (used by the modifier pipeline and resource limits).
///
/// @par Why a separate integer overload?
/// The modifier pipeline and resource pools operate on whole @c int32_t stats,
/// not formula doubles. A dedicated overload keeps those call sites free of
/// @c static_cast noise and preserves exact integer semantics (no rounding of
/// fractional bounds).
[[nodiscard]] constexpr int32_t clampInt(int32_t value, int32_t lo, int32_t hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// Integer minimum (matches std::min on ints, but free of the Windows
/// min/max macro hazard and of any double conversion).
[[nodiscard]] constexpr int minI(int a, int b) noexcept {
  return a < b ? a : b;
}

/// Integer maximum (see @ref minI).
[[nodiscard]] constexpr int maxI(int a, int b) noexcept {
  return a > b ? a : b;
}

/// Integer floor division: the largest integer <= a/b (handles negative
/// operands exactly like floor applied to the rational a/b).
///
/// @par Why a dedicated integer division?
/// The generated specific-mode code compiles formula divisions to integer
/// arithmetic (no floating point in the hot path). A plain C++ `/` truncates
/// toward zero, which is not floor for negatives; this helper makes
/// `floor(a / b)` in a formula byte-identical to the universal double
/// evaluator's `math::floor(a / b)`.
///
/// @par When can a cheaper form be used?
/// Ruleset stats are overwhelmingly non-negative (attributes and skills are
/// stored as unsigned bytes), so most formula divisions never see a negative
/// numerator. For those @ref floorDivN / @ref ceilDivN / @ref roundDivN reduce
/// to a single integer division. Only formulas that can legitimately go
/// negative (e.g. D&D ability modifiers like `(STR - 10) / 2`) need the
/// general form here.
[[nodiscard]] constexpr int floorDiv(int a, int b) noexcept {
  const int q = a / b;
  const int r = a % b;
  return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

/// Integer ceiling division: the smallest integer >= a/b (see @ref floorDiv).
[[nodiscard]] constexpr int ceilDiv(int a, int b) noexcept {
  const int q = a / b;
  const int r = a % b;
  return (r != 0 && ((r > 0) == (b > 0))) ? q + 1 : q;
}

/// Integer division rounded half away from zero (matches std::round applied
/// to the rational a/b; see @ref floorDiv).
[[nodiscard]] constexpr int roundDiv(int a, int b) noexcept {
  const int absB = b < 0 ? -b : b;
  const int r = a % b;
  const int absR = r < 0 ? -r : r;
  if (2 * absR >= absB) {
    return (a < 0) != (b < 0) ? a / b - 1 : a / b + 1;
  }
  return a / b;
}

/// Non-negative floor division: a / b floored, valid only when @c a >= 0 and
/// @c b > 0. For non-negative operands a plain C++ `/` (which truncates toward
/// zero) already equals floor, so this is a single division with no sign
/// handling — the fast path for ruleset stats that never go negative.
///
/// @par Why a separate function instead of assuming it inside floorDiv?
/// Some formulas (D&D ability modifiers) do produce negative numerators; the
/// general @ref floorDiv keeps those exact. This variant documents and
/// exploits the non-negativity precondition so callers who know their values
/// are non-negative (the generated code proves it) pay nothing extra.
[[nodiscard]] constexpr int floorDivN(int a, int b) noexcept {
  return a / b;
}

/// Non-negative ceiling division: ceil(a / b) for @c a >= 0, @c b > 0, as a
/// single division (see @ref floorDivN).
[[nodiscard]] constexpr int ceilDivN(int a, int b) noexcept {
  return (a + b - 1) / b;
}

/// Non-negative division rounded half away from zero: round(a / b) for
/// @c a >= 0, @c b > 0, as a single division (see @ref floorDivN).
[[nodiscard]] constexpr int roundDivN(int a, int b) noexcept {
  return (a + b / 2) / b;
}

/// Converts a formula result (double) to an integer stat value.
///
/// @par Why truncation and not rounding?
/// Formula text is expected to apply @c floor/@c ceil/@c round explicitly where
/// a fractional result must be reduced — the ruleset author, not the engine,
/// decides the rounding policy per stat (e.g. D&D ability modifiers floor,
/// while hit-point averages round down by convention). This helper therefore
/// only performs the type conversion, truncating toward zero, so the engine
/// never silently imposes a rounding rule the ruleset did not ask for.
[[nodiscard]] constexpr int32_t toStat(double value) noexcept {
  return static_cast<int32_t>(value);
}

} // namespace math
} // namespace rpg_os
