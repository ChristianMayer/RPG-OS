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
#include <sstream>
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
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
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
  CHECK(geron.dodge() == 7);           // round(AGI / 2) = round(13 / 2)
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
  // The SRD ruleset stores text descriptions (classes/species/backgrounds/feats)
  // rather than archetype blocks, so construct a fighter directly via its
  // named members. The data database is still loaded from the JSON at runtime.
  DndCharacter fighter;
  fighter.strength = 16;
  fighter.dexterity = 14;
  fighter.constitution = 14;
  fighter.proficiencyBonus = 2;
  fighter.maxHitPoints = 12;
  fighter.hitPoints = 12;

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
  // Same fighter, built directly for the specific (generated) mode and via a
  // JSON record for the universal mode.
  DndCharacter fighter;
  fighter.strength = 16;
  fighter.dexterity = 14;
  fighter.constitution = 14;
  fighter.proficiencyBonus = 2;
  fighter.maxHitPoints = 12;
  fighter.hitPoints = 12;

  auto rng = script({10}); // 10 + STR_mod(3) + prof(2) = 15 >= AC 12
  const CheckResult specific = fighter.dnd5eAttackMelee(fighter, CheckParams{}, rng);
  CHECK(specific.isSuccess);
  CHECK(specific.marginOfSuccess == 3);

  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(readFile("dnd5e_srd.json")));
  rpg_os::DynamicEntity universal(engine.ruleset(), "fighter");
  rpg_os::Json record = rpg_os::Json::parse(
      R"({"attributes":{"STR":16,"DEX":14,"CON":14,"proficiency_bonus":2,"HitPoints_Max":12}})");
  universal.loadFromArchetype(record);
  auto rng2 = script({10});
  const CheckResult universalResult =
      engine.executeCheck("dnd5e_attack_melee", universal, &universal, CheckParams{}, rng2);
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

TEST_CASE("generated code: variance-aware fromCreature (ranged hit points)") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");

  // goblin_warrior carries "HitPoints_Max": "3d6" -> HP in [3, 18].
  auto rngWeak = script({0});
  auto rngStrong = script({0});
  const DndCharacter weakest =
      DndCharacter::fromCreature(ruleset, "goblin_warrior", rpg_os::Variance::Weakest, rngWeak);
  const DndCharacter strongest =
      DndCharacter::fromCreature(ruleset, "goblin_warrior", rpg_os::Variance::Strongest, rngStrong);
  CHECK(weakest.strength == 8); // scalar attributes unchanged
  CHECK(strongest.strength == 8);
  CHECK(weakest.maxHitPoints == 3);
  CHECK(strongest.maxHitPoints == 18);
  CHECK(weakest.hitPoints == 3);
  CHECK(strongest.hitPoints == 18);
}

TEST_CASE("generated code: loadCreatures reads the full bestiary") {
  const rpg_os::Json ruleset = loadRuleset("dnd5e_srd.json");

  // loadCreatures picks every bestiary entry (full SRD bestiary).
  const std::vector<DndCharacter> monsters = DndCharacter::loadCreatures(ruleset);
  REQUIRE(monsters.size() == 330);
  // The SRD bestiary is alphabetical: the first creature is the aboleth
  // (20d10 + 40, so HP falls in [60, 240]).
  CHECK(monsters[0].maxHitPoints >= 60);
  CHECK(monsters[0].maxHitPoints <= 240);
}
