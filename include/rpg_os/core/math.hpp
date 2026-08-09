// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared math helpers for formula evaluation.
//
// These functions are the single source of truth for the formula function set
// (`min max floor ceil round clamp`). Both the universal AST evaluator and the
// generated specific-mode compiled formulas call into this header, so both
// modes agree on exact semantics.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rpg_os {
namespace math {

/// Floors a value (matches std::floor).
[[nodiscard]] inline double floor(double value) noexcept {
  return std::floor(value);
}

/// Ceils a value (matches std::ceil).
[[nodiscard]] inline double ceil(double value) noexcept {
  return std::ceil(value);
}

/// Rounds half away from zero (matches std::round).
[[nodiscard]] inline double round(double value) noexcept {
  return std::round(value);
}

/// Minimum of two values.
[[nodiscard]] inline double min(double a, double b) noexcept {
  return std::min(a, b);
}

/// Maximum of two values.
[[nodiscard]] inline double max(double a, double b) noexcept {
  return std::max(a, b);
}

/// Clamps `value` into the inclusive range [lo, hi].
[[nodiscard]] inline double clamp(double value, double lo, double hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// Integer clamp (used by the modifier pipeline and resource limits).
[[nodiscard]] inline int32_t clampInt(int32_t value, int32_t lo, int32_t hi) noexcept {
  return value < lo ? lo : (value > hi ? hi : value);
}

/// Converts a formula result (double) to an integer stat value.
///
/// Formula text is expected to apply `floor`/`ceil`/`round` explicitly where a
/// fractional result must be reduced; this simply truncates toward zero.
[[nodiscard]] inline int32_t toStat(double value) noexcept {
  return static_cast<int32_t>(value);
}

} // namespace math
} // namespace rpg_os
