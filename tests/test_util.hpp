// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_util.hpp
 * @brief Shared helpers for the rpg_os test suite.
 *
 * Two test doubles are defined here so every test file uses the same ones:
 * @c ScriptedRng (a deterministic, range-checked RNG) and @c MockStats (a
 * minimal @c rpg_os::StatProvider over an unordered map). They are the
 * reason the whole suite can be exact: a scripted RNG replays a fixed sequence
 * of dice, so a check's outcome (and the RNG's consumption order) is pinned by
 * the test rather than being probabilistic.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// Deterministic RNG that returns a fixed script of values in order, each
/// validated to lie within the range requested by the caller.
///
/// @par Why validate each value against the range?
/// The algorithm under test asks for specific ranges (e.g. 1..20 for a d20).
/// Asserting the scripted value lies inside that range catches two bugs at
/// once: a test that hands the RNG a wrong script, and an algorithm that asks
/// for a range the test author did not expect. Both failures surface as a
/// clear CHECK at the exact point of the mismatch.
struct ScriptedRng {
  std::vector<int> values;
  std::size_t idx{0};

  int operator()(int min, int max) {
    REQUIRE(idx < values.size());
    const int value = values[idx++];
    CHECK(value >= min);
    CHECK(value <= max);
    return value;
  }
};

/// Builds a ScriptedRng from an initializer list, e.g. `script({10, 4})` — the
/// terse idiom used throughout the suite to describe "roll a 10, then a 4".
inline ScriptedRng script(std::initializer_list<int> values) {
  return ScriptedRng{std::vector<int>(values)};
}

/// A minimal StatProvider backed by an unordered map (used as a test double).
///
/// @par Why a map instead of a real entity?
/// Check algorithms only require a @c StatProvider; building a full
/// @c rpg_os::DynamicEntity for every unit test would drag in the ruleset
/// loader and JSON machinery. A map-backed double keeps unit tests focused on
/// the algorithm under test, while integration tests exercise the real
/// entities. It also mirrors the universal entity's map semantics closely
/// enough that the templates behave identically.
struct MockStats {
  std::unordered_map<std::string, int32_t> values;

  int32_t getStat(std::string_view id) const {
    const auto it = values.find(std::string(id));
    return it == values.end() ? 0 : it->second;
  }
};
