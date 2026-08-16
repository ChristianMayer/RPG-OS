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

#include <algorithm>
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

TEST_CASE("combat: a mage archetype spec carries its known spells") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec magus;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "magister", magus));
  CHECK(magus.isArchetype);
  CHECK_FALSE(magus.spellIds.empty());
  CHECK(std::find(magus.spellIds.begin(), magus.spellIds.end(), "fulminictus") !=
        magus.spellIds.end());
  CHECK(std::find(magus.spellIds.begin(), magus.spellIds.end(), "ignifaxius") !=
        magus.spellIds.end());
}

TEST_CASE("combat: pickSpell ranks spells by affordability and expected damage") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec magus;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "magister", magus));
  rpg_os::DefaultRandom rng(7);
  auto mage = rpg_os::createFighter(engine, magus, rng);
  REQUIRE(mage != nullptr);
  CHECK(mage->resource("AE") == 35); // 20 + INT 15
  // Full arcane energy: the strongest damaging spell wins (Fulminictus 2d6).
  CHECK(rpg_os::detail::pickSpell(engine, *mage, magus.spellIds) == "fulminictus");
  // With no arcane energy left no spell is affordable.
  (void)mage->modifyResource("AE", -mage->resource("AE"));
  CHECK(mage->resource("AE") == 0);
  CHECK(rpg_os::detail::pickSpell(engine, *mage, magus.spellIds).empty());
  // With only enough AE for the cheap spell, the best affordable one is chosen.
  (void)mage->modifyResource("AE", 4); // only Witch's Claws (4 AE) fits
  CHECK(rpg_os::detail::pickSpell(engine, *mage, magus.spellIds) == "witch_s_claws");
}

TEST_CASE("combat: with magic enabled a mage casts instead of attacking with a weapon") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec magus;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "magister", magus));
  rpg_os::CombatantSpec toad;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "toad", toad));

  // The script pins the magic path: the mage wins initiative (5), passes the
  // SGC/INT/CON casting check (three 1s), and Fulminictus rolls maximum
  // damage (6,6) = 12 -> the toad's 2 LP are gone in one round. A weapon
  // attack consumes a different RNG sequence, so this script only completes
  // when magic (not the sword) is used.
  auto rng = script({5, 1, 1, 1, 1, 6, 6});
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, magus, toad, "tde_attack", "LP", 100, rng);
  CHECK(outcome.winnerIndex == 0);
  CHECK(outcome.rounds == 1);
  CHECK(outcome.remainingLp[0] == 27); // the mage's 27 LP untouched
  CHECK(outcome.remainingLp[1] <= 0);
}

TEST_CASE("combat: magic can be disabled for a pure weapon comparison") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  rpg_os::CombatantSpec magus;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "magister", magus));
  rpg_os::CombatantSpec toad;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "toad", toad));

  // With magic disabled the same mage swings its weapon: it wins initiative
  // (6), rolls a critical attack (1) the toad fails to parry (20), and deals
  // 1d6+4 -> 10, killing the toad in one round.
  auto rng = script({6, 1, 1, 20, 6});
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, magus, toad, "tde_attack", "LP", 100, rng, false);
  CHECK(outcome.winnerIndex == 0);
  CHECK(outcome.rounds == 1);
  CHECK(outcome.remainingLp[1] <= 0);
}

TEST_CASE("combat: dnd5e attack check type and hit-point pool are resolved") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  CHECK(rpg_os::resolveAttackCheckType(engine.ruleset()) == "dnd5e_attack");
  CHECK(rpg_os::resolveHitPointPool(engine.ruleset()) == "HP");
}

TEST_CASE("combat: dnd5e creature spec promotes ac, initiative, and its best attack") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  rpg_os::CombatantSpec spec;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "goblin_warrior", spec));
  CHECK_FALSE(spec.isArchetype);
  CHECK(spec.attackValue == 4);            // best `to_hit` (Scimitar / Shortbow)
  CHECK(spec.damageExpression == "1d6+2"); // its attack damage
  CHECK(spec.acValue == 15);               // bestiary `ac`
  CHECK(spec.initiativeValue == 2);        // bestiary `initiative`
}

TEST_CASE("combat: dnd5e fighter promotes real AC and initiative over the derived stats") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  rpg_os::CombatantSpec goblin;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "goblin_warrior", goblin));
  rpg_os::DefaultRandom rng(1);
  auto fighter = rpg_os::createFighter(engine, goblin, rng);
  REQUIRE(fighter != nullptr);
  // The real bestiary AC (15) overrides the derived 10 + DEX_mod (= 12);
  // initiative exists only via promotion (D&D has no derived Initiative stat).
  CHECK(fighter->getStat("AC") == 15);
  CHECK(fighter->getStat("Initiative") == 2);
  CHECK(fighter->getStat("Attack") == 4); // best `to_hit`
}

TEST_CASE("combat: dnd5e attack-vs-AC resolves to the end") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  rpg_os::CombatantSpec a;
  rpg_os::CombatantSpec b;
  REQUIRE(rpg_os::makeCombatantSpec(engine, "goblin_warrior", a));
  REQUIRE(rpg_os::makeCombatantSpec(engine, "goblin_warrior", b));

  // Scripted RNG: both goblins roll the minimum average hit points (8), A
  // wins initiative (2 + 6 vs 2 + 3), lands its +4 attack (11 + 4 >= AC 15)
  // and rolls maximum damage (1d6+2 = 8) — killing B in one round. Two RNG
  // values are consumed up front for the creatures' average hit-point picks.
  auto rng = script({0, 0, 6, 3, 11, 6});
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(engine, a, b, "dnd5e_attack", "HP", 100, rng);
  CHECK(outcome.winnerIndex == 0);
  CHECK(outcome.rounds == 1);
  CHECK(outcome.remainingLp[0] == 8); // the winner's 8 HP untouched
  CHECK(outcome.remainingLp[1] == 0); // the loser at 0 HP
}
