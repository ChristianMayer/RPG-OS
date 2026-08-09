// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests that load the real shipped rulesets (rulesets/*.json) and validate
// them against the source documents they encode.
#include "test_util.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <rpg_os/universal/engine.hpp>
#include <string>

namespace {

std::string rulesetPath(const char *name) {
  return std::string(RPG_OS_SOURCE_DIR) + "/rulesets/" + name;
}

} // namespace

TEST_CASE("dnd5e_srd: loads and resolves real SRD values") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  CHECK(engine.ruleset().attributes.size() == 9); // 6 abilities + prof + level + HP max
  CHECK(engine.ruleset().skills.size() == 18);
  CHECK(engine.ruleset().checkTypes.size() == 9);
  CHECK(engine.ruleset().costTables.size() == 1);

  auto fighter = engine.createEntity("fighter_lvl1");
  REQUIRE(fighter != nullptr);
  // Ability modifiers from the real SRD table: STR 16 -> +3, CON 14 -> +2.
  CHECK(engine.calculateStat(*fighter, "STR_mod") == 3);
  CHECK(engine.calculateStat(*fighter, "CON_mod") == 2);
  // Base AC = 10 + DEX modifier (DEX 14 -> +2).
  CHECK(engine.calculateStat(*fighter, "AC") == 12);
  // Hit points as set on the archetype (fighter: 10 + CON mod = 12).
  CHECK(fighter->resource("HP") == 12);
  CHECK(fighter->baseAttribute("proficiency_bonus") == 2);

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
  auto fighter = engine.createEntity("fighter_lvl1");
  REQUIRE(fighter != nullptr);
  // Melee: 1d20 + STR_mod(3) + prof(2) vs target AC.
  auto rng = script({10}); // 10 + 3 + 2 = 15
  const rpg_os::CheckResult result = engine.executeCheck("dnd5e_attack_melee", *fighter,
                                                         fighter.get(), rpg_os::CheckParams{}, rng);
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
  // Dodge = AGI / 2 = 13 / 2 = 6.
  CHECK(engine.calculateStat(*geron, "Dodge") == 6);
  // Attack = 6 + COU_Bonus; COU_Bonus = floor((12 - 8) / 3) = 1 -> 7.
  CHECK(engine.calculateStat(*geron, "Attack") == 7);
  // Parry = 3 + AGI_Bonus; AGI_Bonus = floor((13 - 8) / 3) = 1 -> 4.
  CHECK(engine.calculateStat(*geron, "Parry") == 4);

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
  auto geron = engine.createEntity("geron"); // Armor Rating 2
  REQUIRE(geron != nullptr);
  rpg_os::DynamicEntity orc(engine.ruleset(), "orc");

  // 10 raw -> 10 - 2 (AR) = 8 applied; LP 31 -> 23.
  CHECK(engine.applyDamage(orc, *geron, "LP", 10) == -8);
  CHECK(geron->resource("LP") == 23);
  CHECK_FALSE(geron->hasCondition("Wound"));

  // 20 raw -> 18 applied (LP 23 -> 5); 18 > CON 13 -> Wound 1.
  CHECK(engine.applyDamage(orc, *geron, "LP", 20) == -18);
  CHECK(geron->resource("LP") == 5);
  CHECK(geron->hasCondition("Wound"));
  CHECK(geron->conditionStacks("Wound") == 1);
}

TEST_CASE("rulesets: licence/comment metadata and data sections (schema)") {
  for (const char *name : {"dnd5e_srd.json", "tde5e_core.json"}) {
    rpg_os::RulesetEngine engine;
    REQUIRE(engine.loadRulesetFromFile(rulesetPath(name)));
    const rpg_os::Ruleset &rs = engine.ruleset();
    // Licence is required and non-empty; comment is optional but present here.
    CHECK_FALSE(rs.licence.empty());
    CHECK_FALSE(rs.comment.empty());
    // The data database carries the documented sections.
    const rpg_os::Json &data = rs.data;
    CHECK(data.contains("creatures"));
    CHECK(data.contains("spells"));
    CHECK(data.contains("conditions"));
    CHECK(data.contains("poisons"));
    CHECK(data.contains("diseases"));
    CHECK(data.at("conditions").size() > 0);
  }
}

TEST_CASE("dnd5e_srd: full SRD bestiary and spell list are present") {
  rpg_os::RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const rpg_os::Json &data = engine.ruleset().data;
  CHECK(data.at("creatures").size() == 317);
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
  CHECK(data.at("creatures").size() == 10);
  CHECK(data.at("spells").size() >= 50);
  CHECK(data.at("poisons").size() == 5); // toad_poison + Arax/Kelmon/Tulmadron/Wurara

  // A fixed-LP creature (Kosh Toad) is unaffected by variance.
  auto toad = engine.createCreature("toad", rpg_os::Variance::Weakest);
  REQUIRE(toad != nullptr);
  CHECK(toad->resource("LP") == 2);
  auto strong = engine.createCreature("toad", rpg_os::Variance::Strongest);
  REQUIRE(strong != nullptr);
  CHECK(strong->resource("LP") == 2);

  // Real TDE spells carry their check triplets.
  const rpg_os::Json &spells = data.at("spells");
  const auto ignifaxius = std::find_if(spells.begin(), spells.end(), [](const rpg_os::Json &s) {
    return s.value("id", "") == "ignifaxius";
  });
  REQUIRE(ignifaxius != spells.end());
  CHECK(ignifaxius->value("check", "") == "COU/SGC/CHA");
  CHECK(ignifaxius->value("ae_cost", -1) == 8);
}
