// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared helpers for the rpg_os test suite.
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

/// Builds a ScriptedRng from an initializer list.
inline ScriptedRng script(std::initializer_list<int> values) {
  return ScriptedRng{std::vector<int>(values)};
}

/// A minimal StatProvider backed by an unordered map (used as a test double).
struct MockStats {
  std::unordered_map<std::string, int32_t> values;

  int32_t getStat(std::string_view id) const {
    const auto it = values.find(std::string(id));
    return it == values.end() ? 0 : it->second;
  }
};
