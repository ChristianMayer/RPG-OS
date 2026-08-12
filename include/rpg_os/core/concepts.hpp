// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file concepts.hpp
 * @brief Core concepts that both modes (universal + specific) satisfy.
 *
 * These are the seams through which the shared template algorithms in
 * @c core/ talk to entities and RNGs. A single template implementation
 * serves two very different concrete types:
 *   - the universal @c DynamicEntity, which stores stats in hash maps and
 *     looks them up by @c std::string_view;
 *   - the generated specific-mode @c Character classes, which resolve the same
 *     @c std::string_view through a @c constexpr string-to-member switch that
 *     compiles to a jump table (no allocations).
 *
 * @par Why concepts instead of a common base class?
 * The two entity implementations are intentionally very different (one is
 * fully dynamic, the other fully compiled), so forcing them into a shared
 * base class would drag virtual dispatch and dynamic storage into the hot
 * path of the specific mode. Concepts express "anything that can answer these
 * questions" as a compile-time contract: both modes satisfy the same
 * @c StatProvider requirement, so @ref core/checks.hpp can be written once
 * against the concept and instantiated for either entity. Any type that
 * accidentally fails to satisfy the requirement is rejected at compile time
 * with a clear message instead of a link-time mystery.
 */
#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

namespace rpg_os {

/**
 * Anything that yields a uniform integer in [min, max] inclusive.
 *
 * @par Why model randomness as a concept?
 * Check algorithms and dice expressions need randomness, but the *source* of
 * that randomness must be swappable: production uses the entropy-seeded
 * @c DefaultRandom, tests use a scripted RNG that replays a fixed sequence
 * (see @c tests/test_util.hpp), and Monte-Carlo tooling may want a
 * seedable engine. Constraining on this concept lets every algorithm accept
 * any of them without coupling to a concrete class.
 */
template <typename T>
concept RandomNumberGenerator = requires(T &rng, int min, int max) {
  { rng(min, max) } -> std::same_as<int>;
};

/**
 * Anything that can answer "what is the current value of stat `id`?".
 *
 * @par Why any integral type (not a fixed @c int32_t)?
 * Stat values are looked up by their ruleset id, and the algorithms only ever
 * read them (promoting to the computation type where they combine them). That
 * leaves the *storage* type free: the universal entity stores @c int32_t in
 * hash maps, while generated specific-mode characters can store attributes and
 * skills in the narrowest type that fits the ruleset's bounds (@c uint8_t,
 * @c int8_t, ...) — a template over the stored integral type that costs
 * nothing at runtime and shrinks the generated character objects. Both accept
 * the same @c std::string_view key, so the shared algorithm bodies are
 * identical for every storage width.
 */
template <typename T>
concept StatProvider = requires(const T &entity, std::string_view id) {
  { entity.getStat(id) } -> std::integral;
};

/**
 * An entity with no stats: every lookup returns 0.
 *
 * @par Why does this exist?
 * Several checks legitimately have no target — a solo The Dark Eye talent
 * check, or an additive d20 roll against a fixed difficulty rather than a
 * creature's stat (D&D saving throws). Passing a real entity there would be
 * misleading; @c NullStatProvider makes the "no target" case a first-class,
 * stateless argument. The @c static_assert below guarantees it always remains
 * usable wherever a @c StatProvider is expected.
 */
struct NullStatProvider {
  [[nodiscard]] int32_t getStat(std::string_view) const noexcept {
    return 0;
  }
};
static_assert(StatProvider<NullStatProvider>);

} // namespace rpg_os
