// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file entity.hpp
 * @brief Shared entity value semantics.
 *
 * The universal @c DynamicEntity and the generated specific-mode characters
 * do not share a base class — they are intentionally very different animals
 * (hash-map-driven vs. fully compiled). They do, however, share small
 * *value* semantics, most notably resource pools that are clamped to a
 * [min, max] band. Putting those here keeps the two modes on identical logic
 * without forcing an inheritance relationship between them.
 *
 * @par Why share only this?
 * A resource pool is the one piece of entity state whose mutation rules must
 * match exactly across modes: damage must never drive Hit Points below 0 and
 * healing must never exceed the maximum, and the *amount actually applied*
 * must be reported the same way so event triggers and combat logic behave
 * identically. Everything else about an entity (how stats are stored and
 * derived) legitimately differs between the modes and stays where it belongs.
 */
#pragma once

#include <cstdint>
#include <rpg_os/core/math.hpp>

namespace rpg_os {

/**
 * A tracked value clamped to [min, max], e.g. Hit Points, Astral Energy, or
 * Endurance.
 *
 * @par Why clamp inside the pool instead of at every call site?
 * Every ruleset in the engine clamps resources on mutation, and repeating the
 * clamp-and-return-applied logic at every call site would invite drift
 * between the universal engine, the generated code, and the combat loop.
 * Encapsulating it here gives one well-tested rule: `modify` never lets the
 * value leave the bounds and always reports what was *actually* applied, so
 * callers can detect overkill (a dead creature being hit again) or overheal
 * without inspecting the bounds themselves.
 */
struct ResourcePool {
  int32_t current{0};
  int32_t min{0};
  int32_t max{0};

  /// Applies `delta` and clamps into [min, max]. Returns the amount actually
  /// applied (callers can detect overkill or overheal). The @c noexcept
  /// reflects that this is pure integer arithmetic with no allocation.
  [[nodiscard]] int32_t modify(int32_t delta) noexcept {
    const int32_t clamped = math::clampInt(current + delta, min, max);
    const int32_t applied = clamped - current;
    current = clamped;
    return applied;
  }
};

} // namespace rpg_os
