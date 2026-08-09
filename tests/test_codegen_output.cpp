// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_codegen_output.cpp
 * @brief Tests for the generated "specific mode" headers in @c generated/.
 *
 * These headers are produced by codegen/rpg_os_codegen.py from the rulesets.
 * They must compile, expose named members/methods, still load the JSON at
 * runtime (data database), and produce results IDENTICAL to the universal
 * engine for the same scenario — the parity guarantee that motivates the
 * whole dual-mode architecture. Any drift between the code generator and the
 * universal engine fails here.
 */
#include "test_util.hpp"

#include <dnd5e_srd_static.hpp>
#include <doctest/doctest.h>
#include <fstream>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <string_view>
#include <tde5e_core_static.hpp>

namespace {

using rpg_os::CheckParams;
using rpg_os::CheckResult;
using TdeCharacter = rpg_os::generated::tde5e::Character;
using DndCharacter = rpg_os::generated::dnd5e::Character;

std::string readFile(const char *name) {
  const std::string path = std::string(RPG_OS_SOURCE_DIR) + "/rulesets/" + name;
  std::ifstream file(path);
  REQUIRE(file.good());
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

rpg_os::Json loadRuleset(const char *name) {
  return rpg_os::Json::parse(readFile(name));
}

} // namespace

TEST_CASE("generated tde5e: named members, derived getters, and resources") {
  const rpg_os::Json ruleset = loadRuleset("tde5e_core.json");
  const TdeCharacter geron = TdeCharacter::fromArchetype(ruleset, "geron");

  // Named attribute members from the archetype record.
  CHECK(geron.courage == 12);
  CHECK(geron.agility == 13);
  CHECK(geron.constitution == 13);
  CHECK(geron.armorRating == 2);

  // Named derived getters (compiled formulas).
  CHECK(geron.maxLifePoints() == 31);  // 5 + 2 * CON
  CHECK(geron.dodge() == 6);           // AGI / 2
  CHECK(geron.attackSwordsSr6() == 7); // 6 + COU_Bonus
  CHECK(geron.parrySwords() == 4);     // 3 + AGI_Bonus

  // Resources initialized to max.
  CHECK(geron.lifePoints == 31);

  // StatProvider: string -> member / method.
  CHECK(geron.getStat("COU") == 12);
  CHECK(geron.getStat("climbing") == 7);
  CHECK(geron.getStat("LifePoints_Max") == 31);
  CHECK(geron.getStat("nope") == 0);
}

TEST_CASE("generated tde5e: named skill checks match the universal engine (parity)") {
  const rpg_os::Json ruleset = loadRuleset("tde5e_core.json");
  const TdeCharacter geron = TdeCharacter::fromArchetype(ruleset, "geron");

  auto rng = script({14, 12, 11});
  const CheckResult specific = geron.checkClimbing(CheckParams{}, rng);

  CHECK(specific.isSuccess);
  CHECK(specific.remainingPool == 5);
  CHECK(specific.qualityLevel == 2);

  // The same scenario through the universal engine must give identical results.
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(readFile("tde5e_core.json")));
  auto universal = engine.createEntity("geron");
  REQUIRE(universal != nullptr);
  auto rng2 = script({14, 12, 11});
  const CheckResult universalResult =
      engine.executeSkillCheck("climbing", *universal, CheckParams{}, rng2);
  CHECK(universalResult.isSuccess == specific.isSuccess);
  CHECK(universalResult.remainingPool == specific.remainingPool);
  CHECK(universalResult.qualityLevel == specific.qualityLevel);
  CHECK(universalResult.rawDiceRolls == specific.rawDiceRolls);
}

TEST_CASE("generated dnd5e: named members, derived getters, cost table") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");
  const DndCharacter fighter = DndCharacter::fromArchetype(ruleset, "fighter_lvl1");

  CHECK(fighter.strength == 16);
  CHECK(fighter.dexterity == 14);
  CHECK(fighter.strengthModifier() == 3); // floor((16-10)/2)
  CHECK(fighter.constitutionModifier() == 2);
  CHECK(fighter.armorClass() == 12); // 10 + DEX mod
  CHECK(fighter.hitPoints == 12);
  CHECK(fighter.getStat("AC") == 12);

  // Real XP -> level table.
  CHECK(DndCharacter::xpToLevel().lookup(0) == 1);
  CHECK(DndCharacter::xpToLevel().lookup(6500) == 5);
  CHECK(DndCharacter::xpToLevel().lookup(355000) == 20);
}

TEST_CASE("generated dnd5e: attack check matches the universal engine (parity)") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");
  const DndCharacter fighter = DndCharacter::fromArchetype(ruleset, "fighter_lvl1");

  auto rng = script({10}); // 10 + STR_mod(3) + prof(2) = 15 >= AC 12
  const CheckResult specific = fighter.dnd5eAttackMelee(fighter, CheckParams{}, rng);
  CHECK(specific.isSuccess);
  CHECK(specific.marginOfSuccess == 3);

  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(readFile("dnd5e_srd.json")));
  auto universal = engine.createEntity("fighter_lvl1");
  REQUIRE(universal != nullptr);
  auto rng2 = script({10});
  const CheckResult universalResult =
      engine.executeCheck("dnd5e_attack_melee", *universal, universal.get(), CheckParams{}, rng2);
  CHECK(universalResult.isSuccess == specific.isSuccess);
  CHECK(universalResult.marginOfSuccess == specific.marginOfSuccess);
  CHECK(universalResult.rawDiceRolls == specific.rawDiceRolls);
}

TEST_CASE("generated tde5e: loadArchetypes reads every archetype from JSON") {
  const rpg_os::Json ruleset = loadRuleset("tde5e_core.json");
  const std::vector<TdeCharacter> heroes = TdeCharacter::loadArchetypes(ruleset);
  REQUIRE(heroes.size() == 2);
  CHECK(heroes[0].courage == 14); // louisa
  CHECK(heroes[1].courage == 12); // geron
}

TEST_CASE("generated code: variance-aware fromArchetype overloads") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");

  // Weakest/strongest map scalar attributes unchanged and ranged values to the
  // dice bounds. The fighter archetype has no ranges, so both match the base.
  auto rngWeak = script({0});
  auto rngStrong = script({0});
  const DndCharacter weakest =
      DndCharacter::fromArchetype(ruleset, "fighter_lvl1", rpg_os::Variance::Weakest, rngWeak);
  const DndCharacter strongest =
      DndCharacter::fromArchetype(ruleset, "fighter_lvl1", rpg_os::Variance::Strongest, rngStrong);
  CHECK(weakest.strength == 16);
  CHECK(strongest.strength == 16);
  CHECK(weakest.hitPoints == 12);
  CHECK(strongest.hitPoints == 12);
}

TEST_CASE("generated code: variance-aware creatures (ranged hit points)") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");

  // goblin_warrior has "HitPoints_Max": "3d6" -> HP in [3, 18].
  auto rngWeak = script({0});
  auto rngStrong = script({0});
  const DndCharacter weakest =
      DndCharacter::fromCreature(ruleset, "goblin_warrior", rpg_os::Variance::Weakest, rngWeak);
  const DndCharacter strongest =
      DndCharacter::fromCreature(ruleset, "goblin_warrior", rpg_os::Variance::Strongest, rngStrong);
  CHECK(weakest.maxHitPoints == 3);
  CHECK(strongest.maxHitPoints == 18);
  CHECK(weakest.hitPoints == 3);
  CHECK(strongest.hitPoints == 18);

  // loadCreatures picks every bestiary entry (full SRD bestiary).
  const std::vector<DndCharacter> monsters = DndCharacter::loadCreatures(ruleset);
  REQUIRE(monsters.size() == 317);
  // The SRD bestiary is alphabetical: the first creature is the aboleth.
  CHECK(monsters[0].maxHitPoints >= 20);
  CHECK(monsters[0].maxHitPoints <= 240);
}
