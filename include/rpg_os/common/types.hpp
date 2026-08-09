// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file types.hpp
 * @brief Shared value types used by both the universal and the specific mode.
 *
 * This header is the vocabulary every other header builds on. The types live
 * in @c rpg_os::common rather than in either mode's directory on purpose:
 * both the universal engine (which produces them at runtime) and the
 * generated specific-mode code (which consumes them in compiled, inlined
 * check methods) must agree on a single representation of stats and check
 * results. Centralising them here — instead of letting each mode define its
 * own — is what makes the cross-mode parity test meaningful: it proves two
 * different code paths produce byte-identical @c CheckResult values for the
 * same scenario, which would be vacuous if the two sides did not already
 * share these exact types.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpg_os {

/**
 * Integer value used for any stat: attribute, derived stat, or resource pool.
 *
 * @par Why this exact type?
 * A signed 32-bit integer has enough headroom for the additive d20 totals,
 * cost tables, and long-lived resource pools to never overflow in practice,
 * while remaining cheap to copy, compare, and store in the @c unordered_map
 * of a universal entity or the @c constexpr member layout of a generated
 * character. It is also the exact type the @c StatProvider concept requires
 * (see @ref core/concepts.hpp), so a value read from either mode plugs
 * directly into the shared check templates without a conversion step that
 * could disagree between the modes.
 */
using StatValue = int32_t;

/**
 * Result of resolving a single check.
 *
 * @par Why one shared struct?
 * This is deliberately a single, mode-independent value type. The universal
 * engine's generic resolver fills it through a runtime @c switch over the
 * check kind, while generated code fills the very same struct from compiled
 * check methods. Because both modes write into identical fields, a caller
 * (or the parity test) never has to know which mode produced the result —
 * the @em result is the contract, not the code path that created it.
 *
 * The struct is also the natural place to expose every piece of information a
 * rules author or a tabletop app needs to render a check: not just pass/fail,
 * but the criticals that the underlying games treat specially, the margin
 * used for opposed rolls, and the pool/quality information that The Dark
 * Eye's 3d20 talent checks produce. Bundling them avoids spreading check
 * bookkeeping across several ad-hoc return values.
 */
struct CheckResult {
  /// Whether the check succeeded. Criticals override the raw comparison.
  bool isSuccess{false};
  /// Natural critical success (e.g. natural 20, or double-1 on a 3d20 check).
  bool isCriticalSuccess{false};
  /// Natural critical failure (e.g. natural 1, or double-20 on a 3d20 check).
  bool isCriticalFailure{false};
  /// Signed margin: >= 0 on success (higher is better), < 0 on failure.
  /// Kept separate from the raw dice so game logic can grade the check
  /// without having to re-derive the total from the dice themselves.
  int32_t marginOfSuccess{0};
  /// The raw die results, in roll order. Checks that need individual dice
  /// (e.g. a confirmation roll, or a D&D natural-20 detection) read this;
  /// keeping them lets callers verify the outcome independently of the
  /// engine's own interpretation.
  std::vector<int32_t> rawDiceRolls;
  /// Human-readable summary of what happened (logs / debugging). Produced
  /// lazily by callers that want it; always empty by default so the hot path
  /// never pays for string formatting it does not need.
  std::string logDescription;
  /// For pool checks (e.g. DSA 3d20 talent checks): points left over after
  /// compensating attribute overshoots. Zero for non-pool checks.
  int32_t remainingPool{0};
  /// Quality level of a skill check (DSA): 0 means none was computed.
  /// Kept in the shared struct so universal and specific mode report the
  /// same quality bands for the same remaining pool.
  int32_t qualityLevel{0};
};

} // namespace rpg_os
