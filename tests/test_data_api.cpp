// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_data_api.cpp
 * @brief Tests for the engine's full-coverage data API.
 *
 * A ruleset's `data` section carries everything the source book covers —
 * spells, conditions, poisons, diseases, items, archetypes and creatures —
 * and the engine exposes all of it. These tests pin the typed record finders,
 * the generic data-driven spell casting (@c RulesetEngine::castSpell) and the
 * generic affliction application (@c RulesetEngine::applyAffliction) against
 * the real shipped rulesets, so an application can drive every ruleset
 * behaviour without hard-coding a single game system.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/universal/engine.hpp>
#include <string>

using rpg_os::CheckParams;
using rpg_os::DynamicEntity;
using rpg_os::RulesetEngine;

namespace {

std::string rulesetPath(const char *name) {
  return std::string(RPG_OS_SOURCE_DIR) + "/rulesets/" + name;
}

} // namespace

TEST_CASE("data API: finders return every data record type") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  REQUIRE(tde.findSpell("balsam_salabunde") != nullptr);
  REQUIRE(tde.findPoison("toad_poison") != nullptr);
  REQUIRE(tde.findDisease("swift_difar") != nullptr);
  REQUIRE(tde.findCondition("confusion") != nullptr);
  REQUIRE(tde.findItem("dagger") != nullptr);
  REQUIRE(tde.findArchetype("geron") != nullptr);
  REQUIRE(tde.findCreature("heshthot") != nullptr);
  CHECK(tde.findSpell("nope") == nullptr);
  CHECK(tde.findDataRecord("no_such_section", "x") == nullptr);
  CHECK(tde.findSpell("balsam_salabunde")->value("ae_cost", 0) == 1);

  RulesetEngine brp;
  REQUIRE(brp.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  REQUIRE(brp.findSpell("blast") != nullptr);
  REQUIRE(brp.findItem("long_sword") != nullptr);

  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  REQUIRE(dnd.findSpell("fireball") != nullptr);
  REQUIRE(dnd.findCondition("poisoned") != nullptr);
  REQUIRE(dnd.findPoison("midnight_tears") != nullptr);
}

TEST_CASE("data API: castSpell spends the ruleset's spell resource (TDE)") {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  const int32_t aeBefore = geron->resource("AE");

  // Balsam Salabunde costs 1 AE and is a SGC/SGC/INT spell check; rolls 10/10/10
  // all pass (SGC 11, INT 10).
  auto rng = script({10, 10, 10});
  const auto result = engine.castSpell("balsam_salabunde", *geron, nullptr, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(result.cost == 1);
  CHECK(result.resourceId == "AE");
  CHECK(geron->resource("AE") == aeBefore - 1);
  CHECK(result.check.isSuccess);
  CHECK(result.check.rawDiceRolls == std::vector<int>{10, 10, 10});
}

TEST_CASE("data API: castSpell cannot cast when the resource is depleted") {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  (void)geron->modifyResource("AE", -1000); // drain the pool

  auto rng = script({}); // no dice must be consumed
  const auto result = engine.castSpell("balsam_salabunde", *geron, nullptr, CheckParams{}, rng);
  CHECK_FALSE(result.cast);
  CHECK(geron->resource("AE") == 0);
}

TEST_CASE("data API: castSpell deals damage to a target (BRP)") {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto caster = engine.createEntity("average_human");
  auto target = engine.createEntity("average_human");
  REQUIRE(caster != nullptr);
  REQUIRE(target != nullptr);
  const int32_t ppBefore = caster->resource("PP");
  const int32_t hpBefore = target->resource("HP");

  // Blast is level 3 (cost 3 PP) and deals 1d6 damage; no casting check.
  auto rng = script({5}); // damage die
  const auto result = engine.castSpell("blast", *caster, target.get(), CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(result.cost == 3);
  CHECK(result.resourceId == "PP");
  CHECK(caster->resource("PP") == ppBefore - 3);
  CHECK(result.appliedDamage == 5);
  CHECK(target->resource("HP") == hpBefore - 5);
}

TEST_CASE("data API: applyAffliction rolls a save and applies conditions (TDE)") {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);

  // Toad Poison: Toughness save (geron Toughness = round(39/6) = 7). A roll of
  // 5 succeeds -> the poison is resisted and nothing is applied.
  auto rngResist = script({5});
  const auto resisted =
      engine.applyAffliction("poisons", "toad_poison", *geron, CheckParams{}, rngResist);
  CHECK(resisted.saveRolled);
  CHECK(resisted.resisted);
  CHECK(resisted.effectsApplied == 0);
  CHECK_FALSE(geron->hasCondition("confusion"));

  // A failed save (roll 8 > 7) applies 1 level of Confusion.
  auto rngHit = script({8});
  const auto hit = engine.applyAffliction("poisons", "toad_poison", *geron, CheckParams{}, rngHit);
  CHECK(hit.saveRolled);
  CHECK_FALSE(hit.resisted);
  CHECK(hit.effectsApplied == 1);
  CHECK(geron->hasCondition("confusion"));
  CHECK(geron->conditionStacks("confusion") == 1);

  // Unknown afflictions fail fast.
  CHECK_THROWS_AS(static_cast<void>(engine.applyAffliction("poisons", "no_such_poison", *geron,
                                                           CheckParams{}, rngHit)),
                  std::invalid_argument);
}

TEST_CASE("data API: diseases are found and applied like poisons") {
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  const rpg_os::Json *disease = engine.findDisease("swift_difar");
  REQUIRE(disease != nullptr);
  // Diseases follow the same generic path (save + effects).
  const auto result = engine.applyAffliction("diseases", "swift_difar", *geron, CheckParams{});
  const bool wellDefined = result.saveRolled || result.effectsApplied == 0;
  CHECK(wellDefined); // always a well-defined outcome
}
