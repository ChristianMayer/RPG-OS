// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file entity.hpp
 * @ingroup rpg_os_core
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

#include <atomic>
#include <cstdint>
#include <functional>
#include <rpg_os/core/math.hpp>

namespace rpg_os {

/**
 * A unique, stable identifier for a live entity *instance*.
 *
 * @par Why an explicit id type instead of a raw integer?
 * A raw @c uint64_t is already copyable, comparable, and hashable, but it is
 * also silently convertible to and from every other integer in the codebase.
 * A dedicated type makes "this is an entity id" visible at the type level, so
 * a function cannot accidentally accept a damage value where an id belongs.
 * A @c DynamicEntity keeps both its archetype @c id() (a string shared by
 * every instance of a kind) and an @ref EntityId (unique per living
 * instance); the two answer different questions.
 */
struct EntityId {
  uint64_t value{0}; ///< Raw id; 0 is reserved and means "none".
  /// Whether this is a real (non-zero) id.
  [[nodiscard]] constexpr bool valid() const noexcept {
    return value != 0;
  }
  /// The raw id as an integer.
  [[nodiscard]] constexpr uint64_t toUint64() const noexcept {
    return value;
  }
  [[nodiscard]] constexpr bool operator==(const EntityId &) const noexcept = default;
};

/// Returns a fresh, never-reused @ref EntityId.
///
/// @par Why a process-global counter?
/// Entities are created by the engine, by a @ref GameSession, or directly by
/// application code; no single factory sees all of them. A monotonic counter
/// is the simplest way to guarantee that two live entities never collide no
/// matter who created them. The counter lives inside an @c inline function so
/// the header-only library keeps exactly one instance per process.
[[nodiscard]] inline EntityId nextEntityId() noexcept {
  static std::atomic<uint64_t> counter{1};
  return EntityId{counter.fetch_add(1, std::memory_order_relaxed)};
}

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
  /// reflects that this is pure integer arithmetic with no allocation; the
  /// @c constexpr lets the compiler fold constant pool arithmetic.
  [[nodiscard]] constexpr int32_t modify(int32_t delta) noexcept {
    const int32_t clamped = math::clampInt(current + delta, min, max);
    const int32_t applied = clamped - current;
    current = clamped;
    return applied;
  }
};

} // namespace rpg_os

namespace std {
/// Enables @c EntityId as an @c unordered_map / @c unordered_set key.
template <> struct hash<rpg_os::EntityId> {
  [[nodiscard]] std::size_t operator()(const rpg_os::EntityId &id) const noexcept {
    return std::hash<uint64_t>{}(id.value);
  }
};
} // namespace std
