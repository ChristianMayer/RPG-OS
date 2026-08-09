// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared entity helpers.
//
// The universal dynamic entity and the generated specific-mode characters do
// not share a common base class (they are intentionally very different), but
// they do share small value semantics — most notably resource pools that are
// clamped to [min, max]. Those live here so both modes use identical logic.
#pragma once

#include <cstdint>
#include <rpg_os/core/math.hpp>

namespace rpg_os {

/// A tracked value clamped to [min, max], e.g. Hit Points, Astral Energy or
/// Endurance. `modify` never lets the value leave the bounds.
struct ResourcePool {
  int32_t current{0};
  int32_t min{0};
  int32_t max{0};

  /// Applies `delta` and clamps into [min, max]. Returns the amount actually
  /// applied (callers can detect overkill or overheal).
  [[nodiscard]] int32_t modify(int32_t delta) noexcept {
    const int32_t clamped = math::clampInt(current + delta, min, max);
    const int32_t applied = clamped - current;
    current = clamped;
    return applied;
  }
};

} // namespace rpg_os
