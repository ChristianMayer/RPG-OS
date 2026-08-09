// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the combat simulation helpers (include/rpg_os/universal/combat.hpp)
// against the real The Dark Eye 5e ruleset: combatant specs built from the
// bestiary and from archetypes, armour absorption, and fights that run to the
// end (or to the round guard).
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/universal/combat.hpp>
#include <string>

namespace {

std::string rulesetPath(const char *name) {
  return std::string(RPG_OS_SOURCE_DIR) + "/rulesets/" + name;
}

} // namespace

TEST_CASE("combat: attack check type and hit-point pool are resolved") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  CHECK(rpg_os::resolveAttackCheckType(engine.ruleset()) == "tde_attack");
  CHECK(rpg_os::resolveHitPointPool(engine.ruleset()) == "LP");
}

TEST_CASE("combat: bestiary creature spec is built from its attacks and dodge") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec spec;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "gotongi", spec));
  CHECK_FALSE(spec.isArchetype);
  CHECK(spec.name == "Gotongi");
  CHECK(spec.attackValue == 15); // best `to_hit` of its Pinch attack
  CHECK(spec.defenseValue == 9); // bestiary `dodge`
  CHECK(spec.armorRating == 0);
  CHECK(spec.damageExpression == "1d6");
}

TEST_CASE("combat: archetype spec uses derived attack/parry and a weapon") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec spec;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "geron", spec));
  CHECK(spec.isArchetype);
  CHECK(spec.attackValue == 7);  // 6 + COU_Bonus (COU 12)
  CHECK(spec.defenseValue == 4); // floor(SR/2) + AGI_Bonus (AGI 13)
  CHECK(spec.armorRating == 2);
  CHECK(spec.damageExpression == "1d6+4"); // default longsword

  rpg_os::CombatantSpec greatsword;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "geron", greatsword, "2d6"));
  CHECK(greatsword.damageExpression == "2d6");
}

TEST_CASE("combat: a bestiary entry without attacks falls back to derived values") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec spec;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "heshthot", spec));
  CHECK_FALSE(spec.isArchetype);
  CHECK(spec.defenseValue == 7);         // bestiary `dodge`
  CHECK(spec.attackValue == 8);          // 6 + COU_Bonus (COU 16)
  CHECK(spec.damageExpression == "1d6"); // unarmed fallback
}

TEST_CASE("combat: bestiary armour rating is applied through the damage pipeline") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec heshthot;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "heshthot", heshthot)); // armor_rating 2
  rpg_os::DefaultRandom rng(1);
  auto fighter = rpg_os::createFighter(engine, heshthot, rng);
  REQUIRE(fighter != nullptr);
  const int32_t before = fighter->resource("LP");
  CHECK(before == 35);
  rpg_os::DynamicEntity orc(engine.ruleset(), "orc");
  CHECK(engine.applyDamage(orc, *fighter, "LP", 10) == -8); // 10 - Armor_Rating 2
  CHECK(fighter->resource("LP") == before - 8);
}

TEST_CASE("combat: the stronger beast defeats a helpless one to the end") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec irrhalk;
  rpg_os::CombatantSpec toad;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "irrhalk", irrhalk));
  REQUIRE(rpg_os::makeCombatantSpec(engine, "toad", toad));

  // Scripted RNG: irrhalk wins initiative (3 vs 2), lands a critical hit (1)
  // that is not parried (20) and rolls maximum damage on Claws (6, 6) -> 16,
  // which is more than the toad's 2 life points.
  auto rng = script({3, 2, 1, 20, 6, 6});
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, irrhalk, toad, "tde_attack", "LP", 100, rng);
  CHECK(outcome.winnerIndex == 0);
  CHECK(outcome.rounds == 1);
  CHECK(outcome.remainingLp[0] == 90); // Irrhalk untouched
  CHECK(outcome.remainingLp[1] <= 0);  // Toad at 0
}

TEST_CASE("combat: maxRounds guard ends a helpless fight as a draw") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec toad;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "toad", toad)); // damage "0": can never kill
  rpg_os::DefaultRandom rng(1234);
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, toad, toad, "tde_attack", "LP", 5, rng);
  CHECK(outcome.winnerIndex == -1);
  CHECK(outcome.rounds == 5);
  CHECK(outcome.remainingLp[0] > 0);
  CHECK(outcome.remainingLp[1] > 0);
}

TEST_CASE("combat: unknown combatant id is rejected") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec spec;
  CHECK_FALSE(rpg_os::makeCombatantSpec(engine, "no_such_creature", spec));
}
