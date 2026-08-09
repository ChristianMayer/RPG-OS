// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file math.hpp
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

/// Floors a value (matches std::floor). Delegates to the standard function so
/// behaviour stays identical to what a reader of the formula would expect.
[[nodiscard]] inline double floor(double value) noexcept {
  return std::floor(value);
}

/// Ceils a value (matches std::ceil).
[[nodiscard]] inline double ceil(double value) noexcept {
  return std::ceil(value);
}

/// Rounds half away from zero (matches std::round). Note this is *not* the
/// same as "round half up" for negative values; formulas must be written with
/// this convention in mind.
[[nodiscard]] inline double round(double value) noexcept {
  return std::round(value);
}

/// Minimum of two values (matches std::min).
[[nodiscard]] inline double min(double a, double b) noexcept {
  return std::min(a, b);
}

/// Maximum of two values (matches std::max).
[[nodiscard]] inline double max(double a, double b) noexcept {
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
[[nodiscard]] inline double clamp(double value, double lo, double hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// Integer clamp (used by the modifier pipeline and resource limits).
///
/// @par Why a separate integer overload?
/// The modifier pipeline and resource pools operate on whole @c int32_t stats,
/// not formula doubles. A dedicated overload keeps those call sites free of
/// @c static_cast noise and preserves exact integer semantics (no rounding of
/// fractional bounds).
[[nodiscard]] inline int32_t clampInt(int32_t value, int32_t lo, int32_t hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
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
[[nodiscard]] inline int32_t toStat(double value) noexcept {
  return static_cast<int32_t>(value);
}

} // namespace math
} // namespace rpg_os
