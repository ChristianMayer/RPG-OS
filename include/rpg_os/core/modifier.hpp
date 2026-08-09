// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file modifier.hpp
 * @brief Modifier stack pipeline.
 *
 * Derived stats are computed as: base value, then @b Overrides, then @b Add
 * modifiers, then @b Multiply modifiers, then @b Clamps. This header contains
 * the shared, pure pipeline over *already resolved* modifier values:
 *   - universal mode resolves a modifier's formula with the AST evaluator
 *     and then applies this pipeline;
 *   - generated specific-mode code applies this pipeline over compiled base
 *     values with modifier values read from the JSON data records.
 *
 * @par Why a strict phase order?
 * The order is not arbitrary — it mirrors how the source games compose
 * effects: an override replaces the whole value (e.g. a race setting base
 * speed), adds accumulate (equipment bonuses), multiplies scale it (a
 * rage), and clamps enforce hard caps last so no earlier phase can push a
 * stat outside its legal band. Applying all steps of one phase before moving
 * to the next (instead of interleaving per modifier) makes the result
 * independent of the order in which the JSON listed the modifiers — a
 * property the parity test between universal and specific mode depends on.
 */
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

/**
 * A single modifier step for one stat.
 *
 * @par Why one struct with optional fields instead of four types?
 * The modifier set a ruleset attaches to a stat is small and heterogeneous;
 * a single struct with the union of fields keeps the pipeline trivial and
 * lets rulesets list mixed modifiers in one array. Only the fields relevant
 * to a step's @ref type are read, so misuse (e.g. a @c factor on an @c Add)
 * is simply ignored rather than a hard error — consistent with how JSON
 * rulesets are authored.
 */
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
///
/// @par Why four separate passes?
/// Grouping by phase (rather than one pass over the list) is what makes the
/// result independent of modifier order in the JSON — adds always sum first,
/// multiplies always scale the summed total, and clamps always run last. The
/// repeated linear scans are intentional: modifier lists are tiny (a handful
/// of steps), so clarity and order-independence beat micro-optimising.
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
      // D&D-style multiplicative modifiers round down, which is why the
      // multiplication goes through math::floor before narrowing to a stat.
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
