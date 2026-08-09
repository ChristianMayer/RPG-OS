// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Core concepts that both modes (universal + specific) satisfy.
//
// These are the seams through which the shared template algorithms in
// core/ talk to entities and RNGs, so a single implementation serves the
// dynamic universal engine and the generated, fully inlined specific mode.
#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

namespace rpg_os {

/// Anything that yields a uniform integer in [min, max] inclusive.
///
/// Satisfied by `DefaultRandom` (core/dice_engine.hpp) and by deterministic
/// test doubles. Generated code may supply its own RNG.
template <typename T>
concept RandomNumberGenerator = requires(T &rng, int min, int max) {
  { rng(min, max) } -> std::same_as<int>;
};

/// Anything that can answer "what is the current value of stat `id`?".
///
/// Satisfied by the universal `DynamicEntity` (hash-map lookup) and by the
/// generated specific-mode character classes via a `constexpr` string-to-member
/// switch (no allocations, compiles to a jump table).
template <typename T>
concept StatProvider = requires(const T &entity, std::string_view id) {
  { entity.getStat(id) } -> std::same_as<int32_t>;
};

/// An entity with no stats: every lookup returns 0. Used for checks that have
/// no meaningful target (e.g. a solo DSA talent check).
struct NullStatProvider {
  [[nodiscard]] int32_t getStat(std::string_view) const noexcept {
    return 0;
  }
};
static_assert(StatProvider<NullStatProvider>);

} // namespace rpg_os
