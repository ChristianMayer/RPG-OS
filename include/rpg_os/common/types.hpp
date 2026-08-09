// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared value types used by both modes of the engine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpg_os {

/// Integer value used for any stat: attribute, derived stat, or resource
/// pool. All stat arithmetic in rpg_os operates on this type.
using StatValue = int32_t;

/// Result of resolving a single check. Shared by the universal engine's
/// generic resolver and the generated (specific-mode) check methods.
struct CheckResult {
  /// Whether the check succeeded.
  bool isSuccess{false};
  /// Natural critical success (e.g. natural 20, or double-1 on a 3d20 check).
  bool isCriticalSuccess{false};
  /// Natural critical failure (e.g. natural 1, or double-20 on a 3d20 check).
  bool isCriticalFailure{false};
  /// Signed margin: >= 0 on success (higher is better), < 0 on failure.
  int32_t marginOfSuccess{0};
  /// The raw die results, in roll order.
  std::vector<int32_t> rawDiceRolls;
  /// Human-readable summary of what happened (logs / debugging).
  std::string logDescription;
  /// For pool checks (e.g. DSA 3d20 talent checks): points left over after
  /// compensating attribute overshoots. Zero for non-pool checks.
  int32_t remainingPool{0};
  /// Quality level of a skill check (DSA): 0 means none was computed.
  int32_t qualityLevel{0};
};

} // namespace rpg_os
