// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_advancement.cpp
 * @brief Advancement, rests, and curses.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: gainXp levels through the xp_to_level table (phase 5)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  auto sheet = dnd.createCreature("goblin_warrior");
  REQUIRE(sheet != nullptr);
  CHECK(sheet->advancement().level == 1);

  const auto first = dnd.gainXp(*sheet, 299);
  REQUIRE(first.has_value());
  CHECK_FALSE(first->leveled);
  CHECK(sheet->advancement().level == 1);

  const auto second = dnd.gainXp(*sheet, 1); // total 300
  REQUIRE(second.has_value());
  CHECK(second->leveled);
  CHECK(second->toLevel == 2);
  CHECK(sheet->advancement().level == 2);
  CHECK(sheet->advancement().xp() == 300);
}

TEST_CASE("bookkeeping: improvementCost reads TDE AP columns (phase 5)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto cost = tde.improvementCost("AP_Column_C", 1);
  REQUIRE(cost.has_value());
  CHECK(*cost == 3);
  CHECK(tde.improvementCost("no_such_table", 1).error() == BookkeepingError::UnknownCostTable);
}

TEST_CASE("bookkeeping: a long rest restores resources and ends timed conditions (phase 5)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  const int32_t maxLp = geron->resource("LP");
  (void)geron->modifyResource("LP", -10);
  tde.applyCondition(*geron, "pain", 1, 1);
  CHECK(geron->resource("LP") < maxLp);
  CHECK(geron->hasCondition("pain"));

  tde.longRest(*geron);
  CHECK(geron->resource("LP") == maxLp);
  CHECK_FALSE(geron->hasCondition("pain")); // the rest's tick expired it
}

TEST_CASE("bookkeeping: curses apply and can be cured (phase 6)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "data": {
      "conditions": [{"id": "cursed", "name": "Cursed"}],
      "curses": [{
        "id": "wolf_curse", "name": "Wolf Curse",
        "effects": [{"stat": "condition", "amount": 1, "condition": "cursed"}]
      }]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity victim(ruleset, "v");

  auto rng = script({});
  const auto result = engine.applyCurse("wolf_curse", victim, CheckParams{}, rng);
  CHECK(result.effectsApplied == 1);
  CHECK(victim.hasCondition("cursed"));
  REQUIRE(victim.afflictions().size() == 1);

  CHECK(engine.cureAffliction(victim, "curses", "wolf_curse"));
  CHECK_FALSE(victim.hasCondition("cursed"));
  CHECK(victim.afflictions().empty());
  CHECK_FALSE(engine.cureAffliction(victim, "curses", "wolf_curse"));
}
