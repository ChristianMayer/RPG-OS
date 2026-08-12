// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_engine.cpp
 * @brief Integration tests for the universal engine facade (@c rpg_os::RulesetEngine).
 *
 * Exercises the full public API end to end: loading, entity/creature
 * creation with variance, stat calculation, named and skill checks, the
 * JSON-driven damage/event pipeline, and listener registration. Scripted RNGs
 * keep deterministic outcomes, so a regression in the event pipeline or the
 * facade wiring shows up as a hard failure.
 */
#include "test_util.hpp"

#include <cstdio>
#include <doctest/doctest.h>
#include <fstream>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <string_view>

using rpg_os::CheckParams;
using rpg_os::DynamicEntity;
using rpg_os::RulesetEngine;

namespace {

constexpr std::string_view kRuleset = R"json(
{
  "schema_version": 1,
  "ruleset_id": "dsa_demo",
  "ruleset_name": "DSA Demo",
  "licence": "test",
  "namespace": "rpg_os::generated::dsa_demo",
  "attributes": [
    { "id": "COU", "name": "Courage", "min": 1, "max": 21, "default": 10 },
    { "id": "AGI", "name": "Agility", "min": 1, "max": 21, "default": 10 },
    { "id": "STR", "name": "Strength", "min": 1, "max": 21, "default": 10 },
    { "id": "CN",  "name": "Constitution", "min": 1, "max": 21, "default": 10 },
    { "id": "Armor_Rating", "name": "Armor Rating", "min": 0, "max": 20, "default": 0 }
  ],
  "derived_stats": [
    { "id": "Base_AT", "name": "Base Attack", "formula": "floor((COU + AGI + STR) / 5)" },
    { "id": "Vitality_Max", "name": "Max Vitality", "formula": "round((CN + CN + STR) / 2)" }
  ],
  "resource_pools": [
    { "id": "VP", "name": "Vitality", "max_stat": "Vitality_Max", "min": 0 }
  ],
  "skills": [
    { "id": "climbing", "name": "Climbing", "attributes": ["COU", "AGI", "STR"] }
  ],
  "check_types": {
    "dsa4_talent": {
      "resolution": "pool",
      "dice": "3d20",
      "pool_attributes": ["COU", "AGI", "STR"],
      "pool_stat": "climbing",
      "critical_style": "double",
      "fumble_style": "double",
      "grading": "pool_quality",
      "difficulty_mode": "to_stat"
    }
  },
  "event_triggers": [
    {
      "id": "armor_absorption",
      "trigger": "on_damage_calculated",
      "actions": [
        { "type": "modify_event_damage", "formula": "max(0, event.damage - target.Armor_Rating)" }
      ]
    },
    {
      "id": "wound_check",
      "trigger": "on_damage_taken",
      "condition": "event.damage > target.CN",
      "actions": [
        { "type": "apply_condition", "condition": "Wound", "stacks": "floor(event.damage / target.CN)" }
      ]
    }
  ],
  "data": {
    "archetypes": [
      { "id": "geron", "name": "Geron",
        "attributes": { "COU": 12, "AGI": 13, "STR": 11, "CN": 12, "Armor_Rating": 3 },
        "skills": { "climbing": 7 } }
    ]
  }
}
)json";

RulesetEngine makeEngine() {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(kRuleset));
  return engine;
}

} // namespace

TEST_CASE("Engine: loads a ruleset and reports errors") {
  RulesetEngine engine;
  CHECK(engine.loadRulesetFromJson(kRuleset));
  CHECK(engine.loaded());
  CHECK(engine.ruleset().id == "dsa_demo");

  RulesetEngine bad;
  CHECK_FALSE(bad.loadRulesetFromJson(R"({ "schema_version": 1 })"));
  CHECK_FALSE(bad.loaded());
  CHECK_FALSE(bad.lastError().empty());
  CHECK_FALSE(bad.validateRuleset());
}

TEST_CASE("Engine: createEntity loads attributes, skills, and resources") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  CHECK(engine.calculateStat(*geron, "COU") == 12);
  CHECK(engine.calculateStat(*geron, "AGI") == 13);
  CHECK(geron->baseAttribute("STR") == 11);
  CHECK(geron->baseAttribute("Armor_Rating") == 3);
  // Vitality max = round((12 + 12 + 11) / 2) = round(17.5) = 18; current = max.
  CHECK(geron->resource("VP") == 18);
  CHECK(engine.createEntity("nobody") == nullptr);
}

TEST_CASE("Engine: calculateStat computes derived stats") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  // Base_AT = floor((12 + 13 + 11) / 5) = floor(36 / 5) = 7.
  CHECK(engine.calculateStat(*geron, "Base_AT") == 7);
}

TEST_CASE("Engine: executeCheck resolves a DSA skill check (real TDE rules)") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);

  // Unmodified: rolls 14, 12, 11 vs COU 12 / AGI 13 / STR 11; overshoot 2 is
  // paid from the 7 skill points -> 5 left, success, QL 2.
  auto rng = script({14, 12, 11});
  const rpg_os::CheckResult result =
      engine.executeCheck("dsa4_talent", *geron, nullptr, CheckParams{}, rng);
  CHECK(result.isSuccess);
  CHECK(result.remainingPool == 5);
  CHECK(result.qualityLevel == 2);

  // A -4 penalty lowers the EAVs to 8 / 9 / 7; overshoot 6 + 3 + 4 = 13 > 7 -> fail.
  CheckParams penalty;
  penalty.difficulty = -4;
  auto rngPenalty = script({14, 12, 11});
  const rpg_os::CheckResult failed =
      engine.executeCheck("dsa4_talent", *geron, nullptr, penalty, rngPenalty);
  CHECK_FALSE(failed.isSuccess);
  CHECK(failed.remainingPool == -6);
}

TEST_CASE("Engine: executeSkillCheck uses the skill's own linked attributes") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);

  // The climbing skill links COU/AGI/STR and its rating is the pool.
  auto rng = script({14, 12, 11});
  const rpg_os::CheckResult result =
      engine.executeSkillCheck("climbing", *geron, CheckParams{}, rng);
  CHECK(result.isSuccess);
  CHECK(result.remainingPool == 5);
  // executeSkillCheck is [[nodiscard]]; the explicit void cast is required so
  // doctest's CHECK_THROWS_AS (which discards the expression) does not trip
  // -Wunused-result under clang -Werror.
  CHECK_THROWS_AS(
      static_cast<void>(engine.executeSkillCheck("no_such_skill", *geron, CheckParams{}, rng)),
      std::invalid_argument);
}

TEST_CASE("Engine: executeCheck on unknown type throws") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  auto rng = script({5});
  CHECK_THROWS_AS(
      static_cast<void>(engine.executeCheck("no_such_check", *geron, nullptr, CheckParams{}, rng)),
      std::invalid_argument);
}

TEST_CASE("Engine: applyDamage runs armor absorption and wound triggers") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  DynamicEntity attacker(engine.ruleset(), "attacker");
  attacker.setBaseAttribute("STR", 13);

  // 10 raw -> 10 - 3 (AR) = 7 applied. VP 18 -> 11.
  CHECK(engine.applyDamage(attacker, *geron, "VP", 10) == -7);
  CHECK(geron->resource("VP") == 11);
  CHECK_FALSE(geron->hasCondition("Wound"));

  // 20 raw -> 20 - 3 = 17 > CN 12 -> Wound stacks = floor(17 / 12) = 1.
  CHECK(engine.applyDamage(attacker, *geron, "VP", 20) == -11);
  CHECK(geron->resource("VP") == 0);
  CHECK(geron->hasCondition("Wound"));
  CHECK(geron->conditionStacks("Wound") == 1);
}

TEST_CASE("Engine: applyDamage clamps damage at zero") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  DynamicEntity attacker(engine.ruleset(), "attacker");
  // Raw damage below the armor rating reduces to 0.
  CHECK(engine.applyDamage(attacker, *geron, "VP", 2) == 0);
  CHECK(geron->resource("VP") == 18);
}

TEST_CASE("Engine: user event listeners run after rule triggers") {
  RulesetEngine engine = makeEngine();
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  DynamicEntity attacker(engine.ruleset(), "attacker");

  int seenApplied = -1;
  const uint64_t id = engine.registerEventListener(
      rpg_os::EventType::OnDamageTaken,
      [&](const rpg_os::EventData &data) { seenApplied = data.getInt("applied_damage", -1); });
  CHECK(id != 0);

  engine.applyDamage(attacker, *geron, "VP", 10);
  CHECK(seenApplied == 7); // post-armor damage
  CHECK(engine.unregisterEventListener(id));
}

TEST_CASE("Engine: loadRulesetFromFile") {
  const std::string path = "/tmp/rpg_os_test_ruleset.json";
  {
    std::ofstream file(path);
    file << kRuleset;
  }
  RulesetEngine engine;
  CHECK(engine.loadRulesetFromFile(path));
  CHECK(engine.ruleset().id == "dsa_demo");
  std::remove(path.c_str());
}
