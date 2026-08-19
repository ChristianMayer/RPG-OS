// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_traits.cpp
 * @brief Creature traits: stat/check modifiers, effects at creation, regeneration.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: creature traits contribute check and stat modifiers") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "WIS_mod", "name": "Wisdom Mod", "formula": "floor((WIS - 10) / 2)"}],
    "check_types": {
      "mini_perception": {
        "resolution": "threshold", "dice": "1d20", "comparison": "ge",
        "threshold_source": "difficulty", "bonus_stats": ["WIS_mod"]
      }
    },
    "data": {
      "traits": [
        {"id": "keen_smell", "name": "Keen Smell",
         "check_modifiers": [{"scope": "all", "mode": "advantage"}]},
        {"id": "mighty", "name": "Mighty",
         "stat_modifiers": [{"stat": "STR", "type": "add", "value": 2}]}
      ],
      "creatures": [
        {"id": "wolf", "name": "Wolf", "traits": ["keen_smell", "mighty"],
         "attributes": {"STR": 12, "WIS": 12}}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  auto wolf = engine.createCreature("wolf");
  REQUIRE(wolf != nullptr);
  CHECK(wolf->hasTrait("keen_smell"));
  CHECK(wolf->hasTrait("mighty"));
  CHECK_FALSE(wolf->hasTrait("nope"));

  // The trait's stat modifier raises the effective stat.
  CHECK(wolf->getEffectiveStat("STR") == 14);

  // The trait's check modifier gives advantage: roll twice, keep the higher
  // (17 + WIS_mod 1 = 18 >= DC 15 -> success).
  CheckParams params;
  params.difficulty = 15;
  auto rng = script({3, 17});
  const auto result = engine.executeCheck("mini_perception", *wolf, nullptr, params, rng);
  CHECK(result.isSuccess);
  CHECK(result.rawDiceRolls == std::vector<int>{17});

  // Traits survive serialization.
  rpg_os::Json saved;
  wolf->toJson(saved);
  DynamicEntity restored(engine.ruleset(), "wolf");
  restored.fromJson(saved);
  CHECK(restored.hasTrait("keen_smell"));
  CHECK(restored.getEffectiveStat("STR") == 14);
}

TEST_CASE("bookkeeping: migrated D&D creature traits apply via the engine") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  auto wolf = dnd.createCreature("wolf");
  REQUIRE(wolf != nullptr);
  CHECK(wolf->hasTrait("pack_tactics"));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity orc(ruleset, "orc");
  orc.setBaseAttribute("AC", 15);
  CheckParams params;

  // Pack Tactics gives Advantage on attack rolls (wolf STR 14 -> mod +2):
  // roll twice, keep 17; 17 + 2 = 19 >= AC 15.
  auto rng = script({3, 17});
  const auto hit = dnd.executeCheck("dnd5e_attack_melee", *wolf, &orc, params, rng);
  CHECK(hit.isSuccess);
  CHECK(hit.rawDiceRolls == std::vector<int>{17});

  // Without the trait, the same roll misses (3 + 2 = 5 < 15).
  wolf->removeTrait("pack_tactics");
  auto rng2 = script({3});
  const auto miss = dnd.executeCheck("dnd5e_attack_melee", *wolf, &orc, params, rng2);
  CHECK_FALSE(miss.isSuccess);
}

TEST_CASE("bookkeeping: creature trait effects apply at creation") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "data": {
      "traits": [
        {"id": "fire_resistant", "name": "Fire Resistant",
         "effects": [{"kind": "resist", "types": ["Fire"]}]}
      ],
      "creatures": [
        {"id": "salamander", "name": "Salamander", "traits": ["fire_resistant"],
         "attributes": {"STR": 12}}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  auto salamander = engine.createCreature("salamander");
  REQUIRE(salamander != nullptr);
  CHECK(salamander->hasTrait("fire_resistant"));
  // The trait's resist effect was applied when the creature was created.
  CHECK(salamander->hasResistance("Fire"));
}

TEST_CASE("bookkeeping: bless grants bonus dice on attack and save checks") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity ally(ruleset, "ally");
  ally.setBaseAttribute("STR_mod", 3);
  ally.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity orc(ruleset, "orc");
  orc.setBaseAttribute("AC", 15);

  auto rng0 = script({});
  const auto cast = dnd.castSpell("bless", caster, &ally, CheckParams{}, rng0);
  CHECK(cast.cast);
  CHECK(ally.effects().bonusDice().size() == 2);

  // Attack: d20 8 + 5 + bonus 1d4 (4) = 17 >= AC 15.
  auto rng1 = script({8, 4});
  const auto hit = dnd.executeCheck("dnd5e_attack_melee", ally, &orc, CheckParams{}, rng1);
  CHECK(hit.isSuccess);
  CHECK(hit.rawDiceRolls == std::vector<int>{8, 4});

  // Save: d20 8 + STR_mod 3 + bonus 1d4 (2) = 13 >= DC 12 (D&D saves use only
  // the ability modifier).
  CheckParams sp;
  sp.difficulty = 12;
  auto rng2 = script({8, 2});
  const auto save = dnd.executeCheck("dnd5e_save_str", ally, nullptr, sp, rng2);
  CHECK(save.isSuccess);
}

TEST_CASE("bookkeeping: oni regeneration heals at the start of its turns") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  auto oni = dnd.createCreature("oni");
  REQUIRE(oni != nullptr);
  CHECK(oni->hasTrait("regeneration_10"));
  // The recurring heal was registered when the creature was created.
  CHECK(oni->effects().ongoing().size() == 1);

  // Damage the oni, then its turn heals 10 (a constant "10" die draws no RNG).
  (void)oni->modifyResource("HP", -15);
  const int32_t afterDamage = oni->resource("HP");
  auto rng = script({});
  dnd.runTurn(*oni, rng);
  CHECK(oni->resource("HP") == afterDamage + 10);
}

TEST_CASE("bookkeeping: converted regeneration traits heal per amount") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  // The troll regenerates 15 per turn, the oni 10, a severed limb 5.
  auto troll = dnd.createCreature("troll");
  REQUIRE(troll != nullptr);
  CHECK(troll->hasTrait("regeneration_15"));
  CHECK(troll->effects().ongoing().size() == 1);
  (void)troll->modifyResource("HP", -30);
  const int32_t afterDamage = troll->resource("HP");
  auto rng = script({});
  dnd.runTurn(*troll, rng);
  CHECK(troll->resource("HP") == afterDamage + 15);

  auto oni = dnd.createCreature("oni");
  REQUIRE(oni != nullptr);
  CHECK(oni->hasTrait("regeneration_10"));
  (void)oni->modifyResource("HP", -30);
  const int32_t oniAfter = oni->resource("HP");
  auto rng2 = script({});
  dnd.runTurn(*oni, rng2);
  CHECK(oni->resource("HP") == oniAfter + 10);
}

TEST_CASE("bookkeeping: converted D&D creature traits keep their prose") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  // Legendary Resistance is a caller-side rule: the trait exists (so a caller
  // can take note) and its definition preserves the prose.
  auto balor = dnd.createCreature("balor");
  REQUIRE(balor != nullptr);
  CHECK(balor->hasTrait("legendary_resistance_3"));
  CHECK_FALSE(balor->hasRestriction("no_action")); // not an action restriction

  // The engine exposes the trait's prose through the ruleset's data database.
  const rpg_os::Json *trait = dnd.findDataRecord("traits", "legendary_resistance_3");
  REQUIRE(trait != nullptr);
  const std::string desc = trait->value("description", "");
  CHECK(desc.find("can choose to succeed instead") != std::string::npos);
}

TEST_CASE("bookkeeping: converted creature traits shape checks (magic resistance)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  // A deva has Magic Resistance: Advantage on saving throws against spells,
  // expressed as save-advantage check modifiers (same trait as the earlier
  // migrated creatures).
  auto deva = dnd.createCreature("deva");
  REQUIRE(deva != nullptr);
  CHECK(deva->hasTrait("magic_resistance"));
  const auto &ruleset = dnd.ruleset();
  rpg_os::DynamicEntity attacker(ruleset, "attacker");
  attacker.setBaseAttribute("WIS_mod", 3);
  attacker.setBaseAttribute("proficiency_bonus", 2);
  rpg_os::DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("AC", 10);

  // A spell attack against the deva's DEX save is rolled with Advantage.
  CheckParams params;
  params.difficulty = 15;
  auto rng = script({2, 18}); // low then high; advantage keeps 18 + 3 = 21
  const auto result = dnd.executeCheck("dnd5e_save_dex", *deva, nullptr, params, rng);
  CHECK(result.isSuccess);
  CHECK(result.rawDiceRolls == std::vector<int>{18});
}
