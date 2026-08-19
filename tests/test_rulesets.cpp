// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_rulesets.cpp
 * @brief Tests that load the real shipped rulesets (the @c rulesets/ JSON files).
 *
 * Validates the committed data against the source documents they encode and
 * asserts exact counts (creatures, spells, conditions, poisons, diseases) so
 * a botched extraction that silently drops entries is caught. Because these
 * tests read the actual rulesets, they also serve as the living acceptance
 * check that the schema and the data stay in sync.
 */
#include "test_util.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <rpg_os/universal/engine.hpp>
#include <string>

TEST_CASE("dnd5e_srd: loads and resolves real SRD values") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  CHECK(engine.ruleset().attributes.size() == 9); // 6 abilities + prof + level + HP max
  CHECK(engine.ruleset().skills.size() == 18);
  CHECK(engine.ruleset().checkTypes.size() == 15); // + dnd5e_attack (combat sim)
  CHECK(engine.ruleset().costTables.size() == 1);

  // The SRD ruleset stores text descriptions (classes/species/backgrounds/feats)
  // rather than archetype blocks, so build a fighter entity from a JSON record.
  rpg_os::DynamicEntity fighter(engine.ruleset(), "fighter");
  rpg_os::Json record = rpg_os::Json::parse(
      R"({"attributes":{"STR":16,"DEX":14,"CON":14,"INT":10,"WIS":12,"CHA":8,"proficiency_bonus":2,"level":1,"HitPoints_Max":12}})");
  fighter.loadFromArchetype(record);
  // Ability modifiers from the real SRD table: STR 16 -> +3, CON 14 -> +2.
  CHECK(engine.calculateStat(fighter, "STR_mod") == 3);
  CHECK(engine.calculateStat(fighter, "CON_mod") == 2);
  // Base AC = 10 + DEX modifier (DEX 14 -> +2).
  CHECK(engine.calculateStat(fighter, "AC") == 12);
  // Hit points from the record (fighter: 10 + CON mod = 12).
  CHECK(fighter.resource("HP") == 12);
  CHECK(fighter.baseAttribute("proficiency_bonus") == 2);

  // Real XP -> level table (SRD Character Advancement).
  const auto *xp = engine.ruleset().findCostTable("xp_to_level");
  REQUIRE(xp != nullptr);
  CHECK(xp->table.lookup(0) == 1);
  CHECK(xp->table.lookup(2700) == 4);
  CHECK(xp->table.lookup(6500) == 5);
  CHECK(xp->table.lookup(355000) == 20);
}

TEST_CASE("dnd5e_srd: a real attack roll against AC") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  rpg_os::DynamicEntity fighter(engine.ruleset(), "fighter");
  rpg_os::Json record = rpg_os::Json::parse(
      R"({"attributes":{"STR":16,"DEX":14,"CON":14,"proficiency_bonus":2,"HitPoints_Max":12}})");
  fighter.loadFromArchetype(record);
  // Melee: 1d20 + STR_mod(3) + prof(2) vs target AC.
  auto rng = script({10}); // 10 + 3 + 2 = 15
  const rpg_os::CheckResult result =
      engine.executeCheck("dnd5e_attack_melee", fighter, &fighter, rpg_os::CheckParams{}, rng);
  CHECK(result.isSuccess); // 15 >= fighter AC 12
  CHECK(result.marginOfSuccess == 3);
}

TEST_CASE("tde5e_core: loads and resolves real TDE values") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  CHECK(engine.ruleset().attributes.size() == 9); // 8 attributes + armor rating
  CHECK(engine.ruleset().skills.size() > 40);
  CHECK(engine.ruleset().checkTypes.size() == 3);

  auto geron = engine.createEntity("geron");
  REQUIRE(geron != nullptr);
  // Life Points = 5 + 2 * CON (CON 13) = 31; current = max.
  CHECK(engine.calculateStat(*geron, "LifePoints_Max") == 31);
  CHECK(geron->resource("LP") == 31);
  // Dodge = AGI / 2 = 13 / 2 = 6.5, rounded up = 7 (TDE rounds mathematically).
  CHECK(engine.calculateStat(*geron, "Dodge") == 7);
  // Attack = 6 + COU_Bonus; COU_Bonus = floor((12 - 8) / 3) = 1 -> 7.
  CHECK(engine.calculateStat(*geron, "Attack") == 7);
  // Parry = 3 + AGI_Bonus; AGI_Bonus = floor((13 - 8) / 3) = 1 -> 4.
  CHECK(engine.calculateStat(*geron, "Parry") == 4);
  // Spirit = round((COU + SGC + INT) / 6) = round(33 / 6) = 6 (TDE rounds up).
  CHECK(engine.calculateStat(*geron, "Spirit") == 6);
  // Toughness = round((CON + CON + STR) / 6) = round(39 / 6) = 7.
  CHECK(engine.calculateStat(*geron, "Toughness") == 7);

  // Real TDE skill check: climbing links COU/AGI/STR (12/13/13); skill rating 7.
  auto rng = script({14, 12, 11}); // overshoots 2 + 0 + 0 = 2 -> 5 SP left.
  const rpg_os::CheckResult result =
      engine.executeSkillCheck("climbing", *geron, rpg_os::CheckParams{}, rng);
  CHECK(result.isSuccess);
  CHECK(result.remainingPool == 5);
  CHECK(result.qualityLevel == 2);
}

TEST_CASE("tde5e_core: armor absorption and wound triggers (real TDE)") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = engine.createEntity("geron"); // wears leather armour: AR 2 + 3
  REQUIRE(geron != nullptr);
  rpg_os::DynamicEntity orc(engine.ruleset(), "orc");
  CHECK(geron->getEffectiveStat("Armor_Rating") == 5);

  // 10 raw -> 10 - 5 (effective AR) = 5 applied; LP 31 -> 26.
  CHECK(engine.applyDamage(orc, *geron, "LP", 10) == -5);
  CHECK(geron->resource("LP") == 26);
  CHECK_FALSE(geron->hasCondition("Wound"));

  // 20 raw -> 15 applied (LP 26 -> 11); 15 > CON 13 -> Wound 1.
  CHECK(engine.applyDamage(orc, *geron, "LP", 20) == -15);
  CHECK(geron->resource("LP") == 11);
  CHECK(geron->hasCondition("Wound"));
  CHECK(geron->conditionStacks("Wound") == 1);
}

TEST_CASE("rulesets: licence/comment metadata and data sections (schema)") {
  for (const char *name : {"dnd5e_srd.json", "tde5e_core.json", "brp_ugc.json"}) {
    rpg_os::RulesetEngine engine;
    REQUIRE(engine.loadRulesetFromFile(rulesetPath(name)));
    const rpg_os::Ruleset &rs = engine.ruleset();
    // Licence is required and non-empty; comment is optional but present here.
    CHECK_FALSE(rs.licence.empty());
    CHECK_FALSE(rs.comment.empty());
    // The shipped rulesets each state their source and link to where their own
    // licence is declared — the ruleset data is NOT Apache-2.0.
    CHECK_FALSE(rs.source.empty());
    CHECK(rs.licenceSource.find("https://") == 0);
    // The ORC-licensed rulesets carry the verbatim ORC Notice the licence
    // requires; every ruleset carries an attribution/credit statement.
    if (rs.licence.contains("ORC")) {
      CHECK_FALSE(rs.licenceNotice.empty());
    }
    CHECK_FALSE(rs.attribution.empty());
    // The data database carries the documented sections.
    const rpg_os::Json &data = rs.data;
    CHECK(data.contains("creatures"));
    CHECK(data.contains("spells"));
    CHECK(data.contains("conditions"));
    CHECK(data.contains("poisons"));
    CHECK(data.at("conditions").size() > 0);
  }
}

TEST_CASE("dnd5e_srd: full SRD bestiary and spell list are present") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const rpg_os::Json &data = engine.ruleset().data;
  CHECK(data.at("creatures").size() == 330);
  CHECK(data.at("spells").size() >= 320);

  // Ranged hit points are reflected as dice so variance selection works.
  auto goblin = engine.createCreature("goblin_warrior", rpg_os::Variance::Weakest);
  REQUIRE(goblin != nullptr);
  CHECK(goblin->resource("HP") == 3); // 3d6 minimum
  auto strong = engine.createCreature("goblin_warrior", rpg_os::Variance::Strongest);
  REQUIRE(strong != nullptr);
  CHECK(strong->resource("HP") == 18); // 3d6 maximum
}

TEST_CASE("tde5e_core: full core-rule bestiary and spell list are present") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const rpg_os::Json &data = engine.ruleset().data;
  CHECK(data.at("creatures").size() == 11); // 10 bestiary + staff_serpent
  CHECK(data.at("spells").size() >= 50);
  CHECK(data.at("poisons").size() ==
        6); // toad_poison + Arax/Kelmon/Tulmadron/Wurara + staff_serpent_venom

  // A fixed-LP creature (Kosh Toad) is unaffected by variance.
  auto toad = engine.createCreature("toad", rpg_os::Variance::Weakest);
  REQUIRE(toad != nullptr);
  CHECK(toad->resource("LP") == 2);
  auto strong = engine.createCreature("toad", rpg_os::Variance::Strongest);
  REQUIRE(strong != nullptr);
  CHECK(strong->resource("LP") == 2);

  // Staff Serpent (Blessed One transformation form, Core Rules p. 330).
  auto serpent = engine.createCreature("staff_serpent", rpg_os::Variance::Average);
  REQUIRE(serpent != nullptr);
  CHECK(serpent->baseAttribute("COU") == 16);
  CHECK(serpent->resource("LP") == 12);

  // Real TDE spells carry their check triplets.
  const rpg_os::Json &spells = data.at("spells");
  const auto ignifaxius = std::find_if(spells.begin(), spells.end(), [](const rpg_os::Json &s) {
    return s.value("id", "") == "ignifaxius";
  });
  REQUIRE(ignifaxius != spells.end());
  CHECK(ignifaxius->value("check", "") == "COU/SGC/CHA");
  CHECK(ignifaxius->value("ae_cost", -1) == 8);
}

TEST_CASE("brp_ugc: loads and resolves real BRP values") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  CHECK(engine.ruleset().attributes.size() == 10); // 8 characteristics + Luck/Sanity
  CHECK(engine.ruleset().derivedStats.size() == 13);
  CHECK(engine.ruleset().skills.size() == 57);
  CHECK(engine.ruleset().checkTypes.size() == 76);
  CHECK(engine.ruleset().costTables.size() == 0);

  auto human = engine.createEntity("average_human"); // STR 11, CON 11, SIZ 13, POW 11, DEX 11
  REQUIRE(human != nullptr);
  // Hit Points = ceil((CON + SIZ) / 2) = ceil(24 / 2) = 12; Power Points = POW = 11.
  CHECK(human->resource("HP") == 12);
  CHECK(human->resource("PP") == 11);
  // Characteristic x5 rolls: STR_5 = 55, DEX_5 = 55.
  CHECK(engine.calculateStat(*human, "STR_5") == 55);
  CHECK(engine.calculateStat(*human, "DEX_5") == 55);
  // Combat abstractions: Attack = Brawl 25, Parry = Dodge 25, Initiative = DEX 11.
  CHECK(engine.calculateStat(*human, "Attack") == 25);
  CHECK(engine.calculateStat(*human, "Parry") == 25);
  CHECK(engine.calculateStat(*human, "Initiative") == 11);
}

TEST_CASE("brp_ugc: percentile checks grade BRP success levels") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto human = engine.createEntity("average_human"); // Brawl 25
  REQUIRE(human != nullptr);

  // Brawl 25: Critical <= ceil(25/20) = 2, Special <= ceil(25/5) = 5, fumble >= 97.
  auto rSpecial = script({5});
  const rpg_os::CheckResult special =
      engine.executeCheck("brp_skill_brawl", *human, nullptr, rpg_os::CheckParams{}, rSpecial);
  CHECK(special.isSuccess);
  CHECK(special.successLevel == rpg_os::SuccessLevel::Special);

  auto rFail = script({26});
  const rpg_os::CheckResult fail =
      engine.executeCheck("brp_skill_brawl", *human, nullptr, rpg_os::CheckParams{}, rFail);
  CHECK_FALSE(fail.isSuccess);
  CHECK(fail.successLevel == rpg_os::SuccessLevel::Failure);

  auto rFumble = script({97});
  const rpg_os::CheckResult fumble =
      engine.executeCheck("brp_skill_brawl", *human, nullptr, rpg_os::CheckParams{}, rFumble);
  CHECK_FALSE(fumble.isSuccess);
  CHECK(fumble.successLevel == rpg_os::SuccessLevel::Fumble);
}

TEST_CASE("brp_ugc: Easy and Difficult shift the skill rating (BRP)") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto human = engine.createEntity("average_human"); // Brawl 25
  REQUIRE(human != nullptr);

  // Easy: Brawl 25 is doubled to 50, so a roll of 40 succeeds.
  rpg_os::CheckParams easy;
  easy.difficultyScale = rpg_os::DifficultyScale::Easy;
  auto rEasy = script({40});
  const rpg_os::CheckResult ok =
      engine.executeCheck("brp_skill_brawl", *human, nullptr, easy, rEasy);
  CHECK(ok.isSuccess);

  // Difficult: Brawl 25 is halved to 12, so a roll of 15 fails.
  rpg_os::CheckParams difficult;
  difficult.difficultyScale = rpg_os::DifficultyScale::Difficult;
  auto rDifficult = script({15});
  const rpg_os::CheckResult fail =
      engine.executeCheck("brp_skill_brawl", *human, nullptr, difficult, rDifficult);
  CHECK_FALSE(fail.isSuccess);
}

TEST_CASE("brp_ugc: resistance rolls follow the Resistance Table") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto human = engine.createEntity("average_human"); // POW 11
  REQUIRE(human != nullptr);

  // Equal POW (11 vs 11): chance 50. Roll 50 succeeds, roll 51 fails.
  auto rOk = script({50});
  const rpg_os::CheckResult ok =
      engine.executeCheck("brp_resistance_pow", *human, human.get(), rpg_os::CheckParams{}, rOk);
  CHECK(ok.isSuccess);
  auto rFail = script({51});
  const rpg_os::CheckResult fail =
      engine.executeCheck("brp_resistance_pow", *human, human.get(), rpg_os::CheckParams{}, rFail);
  CHECK_FALSE(fail.isSuccess);
}

TEST_CASE("brp_ugc: opposed combat compares success levels (matrix)") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto human = engine.createEntity("average_human"); // Attack 25 (Brawl), Parry 25 (Dodge)
  REQUIRE(human != nullptr);

  // Attacker roll 2 is Critical (2 <= ceil(25/20)); defender roll 10 is
  // Success (10 > ceil(25/5) = 5) -> the Critical strikes.
  auto rHit = script({2, 10});
  const rpg_os::CheckResult hit =
      engine.executeCheck("brp_combat", *human, human.get(), rpg_os::CheckParams{}, rHit);
  CHECK(hit.isSuccess);
  CHECK(hit.successLevel == rpg_os::SuccessLevel::Critical);

  // Both roll Success (10 and 20): equal levels -> the defender parries.
  auto rParried = script({10, 20});
  const rpg_os::CheckResult parried =
      engine.executeCheck("brp_combat", *human, human.get(), rpg_os::CheckParams{}, rParried);
  CHECK_FALSE(parried.isSuccess);
}

TEST_CASE("brp_ugc: major wound and instant death triggers") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  auto human = engine.createEntity("average_human"); // HP max 12
  REQUIRE(human != nullptr);

  // 6 damage = half of max 12 in one blow -> major wound.
  auto target = engine.createEntity("average_human");
  REQUIRE(target != nullptr);
  CHECK(engine.applyDamage(*human, *target, "HP", 6) == -6);
  CHECK(target->resource("HP") == 6);
  CHECK(target->hasCondition("Major_Wound"));

  // 12 damage >= max 12 in one blow -> instant death (Dying).
  auto target2 = engine.createEntity("average_human");
  REQUIRE(target2 != nullptr);
  engine.applyDamage(*human, *target2, "HP", 12);
  CHECK(target2->hasCondition("Dying"));
}

TEST_CASE("brp_ugc: handout data is complete") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  const rpg_os::Json &data = engine.ruleset().data;
  CHECK(data.at("archetypes").size() == 1);
  CHECK(data.at("creatures").size() == 0);
  CHECK(data.at("items").size() == 25);
  CHECK(data.at("spells").size() == 30);
  CHECK(data.at("conditions").size() == 6);
  CHECK(data.at("poisons").size() == 10);
  CHECK(data.at("diseases").size() == 0);

  // Reference tables transcribed from the handout.
  CHECK(data.at("damage_modifier").size() == 8);
  CHECK(data.at("sorcery_spells").size() == 45);
  CHECK(data.at("psychic_abilities").size() == 21);
  CHECK(data.at("superpowers").size() == 33);
  CHECK(data.at("mutations").size() == 30);
  CHECK(data.at("chaotic_features").size() == 50);
  const rpg_os::Json &fumbles = data.at("fumble_tables");
  CHECK(fumbles.at("natural_weapon_attack_parry").size() == 11);
  CHECK(fumbles.at("melee_weapon_attack").size() == 12);
  CHECK(fumbles.at("melee_weapon_parry").size() == 10);
  CHECK(fumbles.at("missile_weapon_attack").size() == 12);
  CHECK(data.at("major_wounds").size() == 15);
  CHECK(data.at("personality_traits").size() == 21);
  CHECK(data.at("reputation").at("gains").size() == 45);
  CHECK(data.at("reputation").at("modifiers").size() == 6);
  CHECK(data.at("sanity").at("temporary_insanity").size() == 6);
  CHECK(data.at("sanity").at("temporary_insanity_duration").size() == 8);

  // Spot-check a few transcribed entries.
  const rpg_os::Json &sorcery = data.at("sorcery_spells");
  const auto cloak = std::find_if(sorcery.begin(), sorcery.end(), [](const rpg_os::Json &s) {
    return s.at("id") == "cloak_of_night";
  });
  REQUIRE(cloak != sorcery.end());
  CHECK((*cloak).at("category") == "Augmentation");
  CHECK((*cloak).at("levels") == "1-4");

  // The average human has HP 12 and PP 11.
  auto human = engine.createEntity("average_human");
  REQUIRE(human != nullptr);
  CHECK(human->resource("HP") == 12);
  CHECK(human->resource("PP") == 11);
}
