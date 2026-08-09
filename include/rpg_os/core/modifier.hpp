// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Modifier stack pipeline.
//
// Derived stats are computed as: base value, then Overrides, then Additive
// modifiers, then Multiplicative modifiers, then Clamps (see the design spec,
// section 6). This header contains the shared, pure pipeline over already
// resolved modifier values:
//   - universal mode resolves a modifier's formula with the AST evaluator and
//     then applies this pipeline;
//   - generated specific-mode code applies this pipeline over compiled base
//     values with modifier values read from the JSON data records.
// This guarantees identical results in both modes.
#pragma once

#include <cstdint>
#include <rpg_os/core/math.hpp>
#include <vector>

namespace rpg_os {

/// The kind of a modifier step.
enum class ModifierType {
  Override, ///< absolute value; the last override in source order wins
  Add,      ///< flat amount added to the current value
  Multiply, ///< multiplies the current value (rounds down, D&D-style)
  Clamp,    ///< clamps the current value into [clampMin, clampMax]
};

/// A single modifier step for one stat. The pipeline applies all modifiers of
/// the same type together, in the order: override, add, multiply, clamp.
struct Modifier {
  ModifierType type{ModifierType::Add};
  /// For Override / Add / Clamp: the value / bound.
  int32_t value{0};
  /// For Multiply: the factor (1.5 == +50%). Ignored otherwise.
  double factor{1.0};
  /// For Clamp: inclusive lower bound.
  int32_t clampMin{0};
  /// For Clamp: inclusive upper bound.
  int32_t clampMax{0};
};

/// Applies the modifier pipeline to `base`:
/// 1. every Override sets the value (last one wins),
/// 2. every Add adds its value,
/// 3. every Multiply replaces the value with floor(value * factor),
/// 4. every Clamp clamps into [clampMin, clampMax].
[[nodiscard]] inline int32_t applyModifierPipeline(int32_t base,
                                                   const std::vector<Modifier> &modifiers) {
  int32_t value = base;
  for (const Modifier &mod : modifiers) {
    if (mod.type == ModifierType::Override) {
      value = mod.value;
    }
  }
  for (const Modifier &mod : modifiers) {
    if (mod.type == ModifierType::Add) {
      value += mod.value;
    }
  }
  for (const Modifier &mod : modifiers) {
    if (mod.type == ModifierType::Multiply) {
      value = math::toStat(math::floor(static_cast<double>(value) * mod.factor));
    }
  }
  for (const Modifier &mod : modifiers) {
    if (mod.type == ModifierType::Clamp) {
      value = math::clampInt(value, mod.clampMin, mod.clampMax);
    }
  }
  return value;
}

} // namespace rpg_os
