// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared modifier pipeline (rpg_os::applyModifierPipeline).
#include <doctest/doctest.h>
#include <rpg_os/core/modifier.hpp>
#include <vector>

using rpg_os::Modifier;
using rpg_os::ModifierType;

namespace {

Modifier add(int32_t value) {
  return Modifier{ModifierType::Add, value, 1.0, 0, 0};
}

Modifier overrideValue(int32_t value) {
  return Modifier{ModifierType::Override, value, 1.0, 0, 0};
}

Modifier multiply(double factor) {
  return Modifier{ModifierType::Multiply, 0, factor, 0, 0};
}

Modifier clamp(int32_t lo, int32_t hi) {
  return Modifier{ModifierType::Clamp, 0, 1.0, lo, hi};
}

} // namespace

TEST_CASE("Modifier pipeline: empty modifiers return the base value") {
  CHECK(rpg_os::applyModifierPipeline(10, {}) == 10);
}

TEST_CASE("Modifier pipeline: additive bonus") {
  CHECK(rpg_os::applyModifierPipeline(10, {add(2)}) == 12);
  CHECK(rpg_os::applyModifierPipeline(10, {add(-3)}) == 7);
  CHECK(rpg_os::applyModifierPipeline(10, {add(1), add(2)}) == 13);
}

TEST_CASE("Modifier pipeline: override sets the value") {
  CHECK(rpg_os::applyModifierPipeline(10, {overrideValue(16)}) == 16);
}

TEST_CASE("Modifier pipeline: last override wins") {
  CHECK(rpg_os::applyModifierPipeline(10, {overrideValue(16), overrideValue(19)}) == 19);
}

TEST_CASE("Modifier pipeline: override is applied before add") {
  CHECK(rpg_os::applyModifierPipeline(10, {overrideValue(16), add(2)}) == 18);
}

TEST_CASE("Modifier pipeline: multiply rounds down") {
  CHECK(rpg_os::applyModifierPipeline(10, {multiply(1.5)}) == 15);
  CHECK(rpg_os::applyModifierPipeline(10, {multiply(0.5)}) == 5);
  // 7 * 1.5 = 10.5 -> floors to 10 (D&D-style round down).
  CHECK(rpg_os::applyModifierPipeline(7, {multiply(1.5)}) == 10);
  // 3 * 1.5 = 4.5 -> floors to 4.
  CHECK(rpg_os::applyModifierPipeline(3, {multiply(1.5)}) == 4);
}

TEST_CASE("Modifier pipeline: clamp bounds the result") {
  CHECK(rpg_os::applyModifierPipeline(10, {add(-20), clamp(0, 100)}) == 0);
  CHECK(rpg_os::applyModifierPipeline(10, {add(200), clamp(0, 20)}) == 20);
  CHECK(rpg_os::applyModifierPipeline(10, {clamp(0, 20)}) == 10);
}

TEST_CASE("Modifier pipeline: full pipeline in spec order") {
  // base 10 -> override 16 -> add 2 -> multiply 2 -> clamp max 30
  const std::vector<Modifier> mods = {overrideValue(16), add(2), multiply(2.0), clamp(0, 30)};
  CHECK(rpg_os::applyModifierPipeline(10, mods) == 30);
  // Same, without the clamp capping: 16 + 2 = 18, * 2 = 36.
  const std::vector<Modifier> modsNoClamp = {overrideValue(16), add(2), multiply(2.0)};
  CHECK(rpg_os::applyModifierPipeline(10, modsNoClamp) == 36);
}
