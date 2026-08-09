// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for variance / range selection (rpg_os::Variance, rpg_os::readVariantValue).
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <fstream>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/engine.hpp>
#include <string>

using rpg_os::Variance;

TEST_CASE("Variance: pickVariant honours min/max/third selection") {
  // Deterministic scripted RNG always returns 0 from rng(0, span).
  auto rng = script({0, 0, 0, 0, 0});
  CHECK(rpg_os::pickVariant(0, 30, Variance::Weakest, rng) == 0);
  CHECK(rpg_os::pickVariant(0, 30, Variance::Strongest, rng) == 30);
  CHECK(rpg_os::pickVariant(0, 30, Variance::Weak, rng) <= 10);    // lower third
  CHECK(rpg_os::pickVariant(0, 30, Variance::Average, rng) >= 10); // middle third
  CHECK(rpg_os::pickVariant(0, 30, Variance::Average, rng) <= 20);
  CHECK(rpg_os::pickVariant(0, 30, Variance::Strong, rng) >= 20); // upper third
  // Random stays within the full range.
  CHECK(rpg_os::pickVariant(5, 5, Variance::Random, rng) == 5); // degenerate
}

TEST_CASE("Variance: readVariantValue handles scalars, ranges, and dice") {
  rpg_os::DefaultRandom rng;

  // Plain integer: no range, same for every variance.
  CHECK(rpg_os::readVariantValue(rpg_os::Json(7), Variance::Weakest, rng) == 7);
  CHECK(rpg_os::readVariantValue(rpg_os::Json(7), Variance::Strongest, rng) == 7);

  // Explicit min/max range.
  const rpg_os::Json range = {{"min", 2}, {"max", 12}};
  CHECK(rpg_os::readVariantValue(range, Variance::Weakest, rng) == 2);
  CHECK(rpg_os::readVariantValue(range, Variance::Strongest, rng) == 12);
  const int32_t weak = rpg_os::readVariantValue(range, Variance::Weak, rng);
  CHECK(weak >= 2);
  CHECK(weak <= 2 + 10 / 3);
  const int32_t strong = rpg_os::readVariantValue(range, Variance::Strong, rng);
  CHECK(strong >= 2 + 20 / 3);
  CHECK(strong <= 12);
  const int32_t random = rpg_os::readVariantValue(range, Variance::Random, rng);
  CHECK(random >= 2);
  CHECK(random <= 12);

  // Dice expression: weakest/strongest map to the dice bounds, random rolls.
  CHECK(rpg_os::readVariantValue(rpg_os::Json("2d6"), Variance::Weakest, rng) == 2);
  CHECK(rpg_os::readVariantValue(rpg_os::Json("2d6"), Variance::Strongest, rng) == 12);
  const int32_t rolled = rpg_os::readVariantValue(rpg_os::Json("2d6"), Variance::Random, rng);
  CHECK(rolled >= 2);
  CHECK(rolled <= 12);
}

TEST_CASE("Engine: createCreature with variance selects ranged hit points") {
  // Reuse the shipped dnd5e ruleset (goblin warrior has "HitPoints_Max": "3d6",
  // so its HP resource ranges over [3, 18]).
  const std::string path = std::string(RPG_OS_SOURCE_DIR) + "/rulesets/dnd5e_srd.json";
  std::ifstream file(path);
  REQUIRE(file.good());
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(
      std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>())));

  auto weakest = engine.createCreature("goblin_warrior", Variance::Weakest);
  auto strongest = engine.createCreature("goblin_warrior", Variance::Strongest);
  REQUIRE(weakest != nullptr);
  REQUIRE(strongest != nullptr);
  CHECK(weakest->baseAttribute("STR") == 8); // scalar attributes unchanged
  CHECK(strongest->baseAttribute("STR") == 8);
  CHECK(weakest->getStat("HitPoints_Max") == 3);
  CHECK(strongest->getStat("HitPoints_Max") == 18);
  CHECK(weakest->resource("HP") == 3);
  CHECK(strongest->resource("HP") == 18);

  // Random stays within the dice bounds.
  auto random = engine.createCreature("goblin_warrior", Variance::Random);
  REQUIRE(random != nullptr);
  CHECK(random->resource("HP") >= 3);
  CHECK(random->resource("HP") <= 18);
}
