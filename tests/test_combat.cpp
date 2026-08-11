// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_combat.cpp
 * @brief Tests for the combat simulation helpers
 * (@c include/rpg_os/universal/combat.hpp).
 *
 * Runs against the real The Dark Eye 5e ruleset: combatant specs built from
 * the bestiary and from archetypes, armour absorption through the event
 * pipeline, and fights that run to the end (or to the round guard). Scripted
 * RNGs keep the fights deterministic, so the combat loop's RNG consumption
 * order is pinned by these tests.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
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

TEST_CASE("combat: a bestiary entry with attacks uses its best attack") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec spec;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "heshthot", spec));
  CHECK_FALSE(spec.isArchetype);
  CHECK(spec.defenseValue == 7);           // bestiary `dodge`
  CHECK(spec.attackValue == 16);           // best `to_hit` (Long Sword / Whip)
  CHECK(spec.damageExpression == "1d6+5"); // the Long Sword's DP
}

TEST_CASE("combat: a bestiary entry without attacks falls back to derived values") {
  // The shipped Heshthot has attacks, so strip them from a loaded copy to
  // exercise the fallback path (no natural attack defined).
  const std::string path = rulesetPath("tde5e_core.json");
  std::ifstream file(path);
  REQUIRE(file.good());
  rpg_os::Json ruleset = rpg_os::Json::parse(
      std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
  for (rpg_os::Json &creature : ruleset["data"]["creatures"]) {
    if (creature.value("id", "") == "heshthot") {
      creature.erase("attacks");
    }
  }
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(ruleset.dump()));
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

TEST_CASE("combat: brp_ugc opposed percentile combat runs to the end") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  CHECK(rpg_os::resolveAttackCheckType(engine.ruleset()) == "brp_combat");
  CHECK(rpg_os::resolveHitPointPool(engine.ruleset()) == "HP");

  // Archetype: Attack = Brawl 25, Parry = Dodge 25, custom weapon.
  rpg_os::CombatantSpec humanA;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "average_human", humanA, "1d12"));
  CHECK(humanA.isArchetype);
  CHECK(humanA.attackValue == 25);
  CHECK(humanA.defenseValue == 25);

  // Scripted RNG: human A wins initiative (13 vs 12), lands a critical (1,
  // <= ceil(25/20) = 2) that is not parried (50 > 25) and rolls maximum
  // damage (12) -> the other human's 12 hit points are gone in one round.
  auto rng = script({2, 1, 1, 50, 12});
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, humanA, humanA, "brp_combat", "HP", 100, rng);
  CHECK(outcome.winnerIndex == 0);
  CHECK(outcome.rounds == 1);
  CHECK(outcome.remainingLp[0] == 12); // attacker untouched
  CHECK(outcome.remainingLp[1] == 0);  // defender at 0
}
