// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_conditions.cpp
 * @brief Mechanical conditions: durations, stat/check modifiers, restrictions and capabilities.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: conditions apply, tick, and expire (phase 3)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto sheet = tde.createEntity("geron");
  REQUIRE(sheet != nullptr);

  tde.applyCondition(*sheet, "pain", 2, 2, "test");
  CHECK(sheet->hasCondition("pain"));
  CHECK(sheet->conditionStacks("pain") == 2);
  CHECK(sheet->effects().stacks("pain") == 2);

  CHECK(tde.tickEffects(*sheet) == 0); // 2 -> 1 remaining
  CHECK(sheet->hasCondition("pain"));
  CHECK(tde.tickEffects(*sheet) == 1); // 1 -> 0: expires
  CHECK_FALSE(sheet->hasCondition("pain"));
  CHECK_FALSE(sheet->effects().has("pain"));

  // unknown conditions fail fast
  CHECK_THROWS_AS(tde.applyCondition(*sheet, "no_such", 1, 0), std::invalid_argument);

  // removeCondition clears both the stack map and the timeline
  tde.applyCondition(*sheet, "pain", 1, 0);
  CHECK(sheet->hasCondition("pain"));
  tde.removeCondition(*sheet, "pain");
  CHECK_FALSE(sheet->hasCondition("pain"));
}

TEST_CASE("bookkeeping: a condition's stat modifiers affect effective stats (phase 3)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "data": {
      "conditions": [{
        "id": "weakened", "name": "Weakened",
        "stat_modifiers": [{"stat": "STR", "type": "add", "value": -2}]
      }]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity sheet(ruleset, "test");
  sheet.setBaseAttribute("STR", 12);
  CHECK(sheet.getEffectiveStat("STR") == 12);

  engine.applyCondition(sheet, "weakened", 1, 0);
  CHECK(sheet.getEffectiveStat("STR") == 10);     // -2 per stack
  engine.applyCondition(sheet, "weakened", 2, 0); // 3 stacks total
  CHECK(sheet.getEffectiveStat("STR") == 6);      // 12 - 2*3
}

TEST_CASE("bookkeeping: a condition's check modifiers shape the carrier's checks") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "STR_mod", "name": "Strength Mod", "formula": "floor((STR - 10) / 2)"}],
    "check_types": {
      "mini_attack": {
        "resolution": "threshold", "dice": "1d20", "comparison": "ge",
        "threshold_source": "difficulty", "bonus_stats": ["STR_mod"]
      }
    },
    "data": {
      "conditions": [
        {
          "id": "debilitated", "name": "Debilitated",
          "check_modifiers": [
            {"scope": "all", "bonus": -2, "per_stack": true},
            {"scope": "mini_attack", "mode": "disadvantage"}
          ]
        },
        {
          "id": "exposed", "name": "Exposed",
          "check_modifiers": [
            {"scope": "mini_attack", "mode": "advantage", "side": "target"}
          ]
        }
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity fighter(ruleset, "fighter");
  fighter.setBaseAttribute("STR", 12);
  DynamicEntity other(ruleset, "other");

  CheckParams params;
  params.difficulty = 15; // DC 15

  // No conditions: 10 + 1 = 11 < 15 -> miss (one d20 consumed).
  auto r1 = script({10});
  CHECK_FALSE(engine.executeCheck("mini_attack", fighter, nullptr, params, r1).isSuccess);

  // Debilitated 1 stack: -2 bonus AND disadvantage. Disadvantage keeps 6,
  // 6 + 1 - 2 = 5 -> miss; the kept roll is recorded.
  engine.applyCondition(fighter, "debilitated", 1, 0);
  auto r2 = script({14, 6});
  const auto poor = engine.executeCheck("mini_attack", fighter, nullptr, params, r2);
  CHECK_FALSE(poor.isSuccess);
  CHECK(poor.rawDiceRolls == std::vector<int>{6});

  // Debilitated 2 stacks: the per-stack bonus grows to -4.
  // Disadvantage keeps 19; 19 + 1 - 4 = 16 >= 15 -> hit.
  engine.applyCondition(fighter, "debilitated", 1, 0);
  auto r3 = script({20, 19});
  CHECK(engine.executeCheck("mini_attack", fighter, nullptr, params, r3).isSuccess);

  // Target-side: when the *other* entity is Exposed, attacks against it gain
  // advantage for the attacker (18 + 1 = 19 -> hit).
  engine.removeCondition(fighter, "debilitated");
  engine.applyCondition(other, "exposed", 1, 0);
  auto r4 = script({7, 18});
  CHECK(engine.executeCheck("mini_attack", fighter, &other, params, r4).isSuccess);
}

TEST_CASE("bookkeeping: D&D condition check modifiers shape attack rolls both ways") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity fighter(ruleset, "fighter");
  fighter.setBaseAttribute("STR_mod", 3);
  fighter.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity orc(ruleset, "orc");
  orc.setBaseAttribute("AC", 15);
  const CheckParams params;

  // Blinded fighter: its own attack rolls have Disadvantage.
  dnd.applyCondition(fighter, "blinded", 1, 0);
  auto rng1 = script({8, 17}); // would hit on 17, but disadvantage keeps 8
  const auto poor = dnd.executeCheck("dnd5e_attack_melee", fighter, &orc, params, rng1);
  CHECK_FALSE(poor.isSuccess);
  CHECK(poor.rawDiceRolls == std::vector<int>{8});

  // The orc is blinded: attacks against it have Advantage for the fighter.
  DynamicEntity clean(ruleset, "clean");
  clean.setBaseAttribute("STR_mod", 3);
  clean.setBaseAttribute("proficiency_bonus", 2);
  dnd.applyCondition(orc, "blinded", 1, 0);
  auto rng2 = script({8, 17}); // advantage keeps 17 -> 17 + 5 = 22 >= 15 hit
  const auto good = dnd.executeCheck("dnd5e_attack_melee", clean, &orc, params, rng2);
  CHECK(good.isSuccess);
  CHECK(good.rawDiceRolls == std::vector<int>{17});
}

TEST_CASE("bookkeeping: auto_fail check modifiers fail matching checks") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "STR_mod", "name": "Str Mod", "formula": "floor((STR - 10) / 2)"}],
    "check_types": {
      "save_str": {"resolution": "threshold", "dice": "1d20", "comparison": "ge",
                   "threshold_source": "difficulty", "bonus_stats": ["STR_mod"]},
      "check_con": {"resolution": "threshold", "dice": "1d20", "comparison": "ge",
                    "threshold_source": "difficulty", "bonus_stats": ["STR_mod"]}
    },
    "data": {
      "conditions": [
        {"id": "paralyzed", "name": "Paralyzed",
         "check_modifiers": [{"scope": "save_str", "mode": "auto_fail"}]}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity hero(ruleset, "hero");
  hero.setBaseAttribute("STR", 12);
  engine.applyCondition(hero, "paralyzed", 1, 0);

  CheckParams params;
  params.difficulty = 15;
  // The STR save is auto-failed without rolling (RNG index stays 0).
  auto rng1 = script({20});
  const auto failed = engine.executeCheck("save_str", hero, nullptr, params, rng1);
  CHECK_FALSE(failed.isSuccess);
  CHECK(rng1.idx == 0);

  // An unrelated check is unaffected.
  auto rng2 = script({20});
  const auto ok = engine.executeCheck("check_con", hero, nullptr, params, rng2);
  CHECK(ok.isSuccess);
}

TEST_CASE("bookkeeping: paralyzed creatures auto-fail STR and DEX saves") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity hero(ruleset, "hero");
  hero.setBaseAttribute("STR_mod", 3);
  hero.setBaseAttribute("DEX_mod", 3);
  hero.setBaseAttribute("WIS_mod", 3);
  hero.setBaseAttribute("proficiency_bonus", 2);
  dnd.applyCondition(hero, "paralyzed", 1, 0);

  CheckParams params;
  params.difficulty = 15;
  // STR and DEX saves auto-fail without rolling (RNG index stays 0).
  auto rng1 = script({20});
  CHECK_FALSE(dnd.executeCheck("dnd5e_save_str", hero, nullptr, params, rng1).isSuccess);
  CHECK(rng1.idx == 0);
  auto rng2 = script({20});
  CHECK_FALSE(dnd.executeCheck("dnd5e_save_dex", hero, nullptr, params, rng2).isSuccess);
  // A non-STR/DEX save is unaffected.
  auto rng3 = script({20});
  CHECK(dnd.executeCheck("dnd5e_save_wis", hero, nullptr, params, rng3).isSuccess);
}

TEST_CASE("bookkeeping: restrictions are queryable and enforced in castSpell") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10}],
    "resource_pools": [{"id": "MP", "name": "Mana", "max_stat": "WIS", "min": 0}],
    "spell_resource": "MP",
    "data": {
      "conditions": [
        {"id": "stunned", "name": "Stunned", "restrictions": ["no_action", "no_cast", "no_move"]}
      ],
      "spells": [
        {"id": "cantrip", "name": "Cantrip", "level": 1, "cost": 1}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("WIS", 10);
  caster.loadFromArchetype(rpg_os::Json::object()); // initialise MP pool
  CHECK(caster.resource("MP") == 10);

  // Without a restriction the spell casts and the cost is paid.
  auto rngOk = script({});
  const auto ok = engine.castSpell("cantrip", caster, nullptr, CheckParams{}, rngOk);
  CHECK(ok.cast);
  CHECK(caster.resource("MP") == 9);

  // A stunned creature cannot act or cast: the restriction is queryable...
  engine.applyCondition(caster, "stunned", 1, 0);
  CHECK(caster.hasRestriction("no_action"));
  CHECK(caster.hasRestriction("no_cast"));
  CHECK(caster.hasRestriction("no_move"));
  CHECK_FALSE(engine.actionAllowed(caster, "action"));
  CHECK_FALSE(engine.actionAllowed(caster, "cast"));
  CHECK_FALSE(engine.actionAllowed(caster, "move"));
  CHECK(engine.actionAllowed(caster, "speak"));

  // ...and enforced: the cast is denied before any resource is spent.
  auto rngDenied = script({});
  const auto denied = engine.castSpell("cantrip", caster, nullptr, CheckParams{}, rngDenied);
  CHECK_FALSE(denied.cast);
  CHECK(denied.denied == "no_cast");
  CHECK(caster.resource("MP") == 9); // cost NOT spent
  CHECK(rngDenied.idx == 0);
}

TEST_CASE("bookkeeping: restrictions come from traits too and block casting") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10}],
    "resource_pools": [{"id": "MP", "name": "Mana", "max_stat": "WIS", "min": 0}],
    "spell_resource": "MP",
    "data": {
      "traits": [
        {"id": "mute", "name": "Mute", "restrictions": ["no_cast"]}
      ],
      "creatures": [
        {"id": "mute_being", "name": "Mute Being", "traits": ["mute"],
         "attributes": {"WIS": 10}}
      ],
      "spells": [
        {"id": "cantrip", "name": "Cantrip", "level": 1, "cost": 1}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  auto being = engine.createCreature("mute_being");
  REQUIRE(being != nullptr);
  CHECK(being->hasTrait("mute"));
  CHECK(being->hasRestriction("no_cast"));
  CHECK_FALSE(being->hasRestriction("no_action"));
  CHECK_FALSE(engine.actionAllowed(*being, "cast"));
  CHECK(engine.actionAllowed(*being, "action"));

  // The trait's restriction is enforced at cast time.
  auto rng = script({});
  const auto denied = engine.castSpell("cantrip", *being, nullptr, CheckParams{}, rng);
  CHECK_FALSE(denied.cast);
  CHECK(denied.denied == "no_cast");
  CHECK(rng.idx == 0);
}

TEST_CASE("bookkeeping: capabilities are queryable on traits and conditions") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "data": {
      "traits": [
        {"id": "amphibious", "name": "Amphibious", "capabilities": ["swim", "breath_water"]}
      ],
      "conditions": [
        {"id": "blessed", "name": "Blessed", "capabilities": ["see_invisible"]}
      ],
      "creatures": [
        {"id": "frog", "name": "Frog", "traits": ["amphibious"], "attributes": {"STR": 10}}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  auto frog = engine.createCreature("frog");
  REQUIRE(frog != nullptr);
  CHECK(frog->hasCapability("swim"));
  CHECK(frog->hasCapability("breath_water"));
  CHECK_FALSE(frog->hasCapability("fly"));
  const auto caps = frog->capabilities();
  CHECK(caps == std::vector<std::string>({"swim", "breath_water"}));

  // Conditions contribute capabilities while active.
  engine.applyCondition(*frog, "blessed", 1, 0);
  CHECK(frog->hasCapability("see_invisible"));
  CHECK_FALSE(frog->hasCapability("fly"));
  engine.removeCondition(*frog, "blessed"); // expires
  CHECK_FALSE(frog->hasCapability("see_invisible"));
}

TEST_CASE("bookkeeping: attackOnce enforces the attacker's restrictions") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10},
                   {"id": "AC", "name": "Armor Class", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "WIS_mod", "name": "Wisdom Mod", "formula": "floor((WIS - 10) / 2)"}],
    "resource_pools": [{"id": "HP", "name": "Hit Points", "max_stat": "STR", "min": 0}],
    "check_types": {
      "mini_attack": {
        "resolution": "threshold", "dice": "1d20", "comparison": "ge",
        "threshold_source": "target_stat", "threshold_stat": "AC",
        "bonus_stats": ["WIS_mod"]
      }
    },
    "data": {
      "conditions": [
        {"id": "stunned", "name": "Stunned", "restrictions": ["no_action", "no_move"]}
      ],
      "creatures": [
        {"id": "fighter", "name": "Fighter", "attributes": {"STR": 20, "WIS": 14, "AC": 10}}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  auto attacker = engine.createCreature("fighter");
  REQUIRE(attacker != nullptr);
  attacker->loadFromArchetype(rpg_os::Json::object()); // HP pool = STR 20
  rpg_os::DynamicEntity defender(engine.ruleset(), "defender");
  defender.setBaseAttribute("STR", 20);
  defender.setBaseAttribute("AC", 10);
  defender.loadFromArchetype(rpg_os::Json::object()); // HP pool

  const rpg_os::DiceExpression damage("1d6");
  // Unrestricted: the attack resolves (17 + WIS_mod 2 = 19 >= AC 10 -> hit);
  // 1d6 = 4 damage leaves the defender at 16 HP (not dead, so false).
  auto rngHit = script({17, 4});
  CHECK_FALSE(
      rpg_os::detail::attackOnce(engine, *attacker, damage, defender, "mini_attack", "HP", rngHit));
  CHECK(defender.resource("HP") == 16);
  CHECK(attacker->resource("HP") == 20); // attacker unharmed

  // Stunned: no action allowed — the attack is refused and no dice are rolled.
  engine.applyCondition(*attacker, "stunned", 1, 0);
  auto rngDenied = script({17, 4});
  CHECK_FALSE(rpg_os::detail::attackOnce(engine, *attacker, damage, defender, "mini_attack", "HP",
                                         rngDenied));
  CHECK(rngDenied.idx == 0);
}

TEST_CASE("bookkeeping: converted D&D creature traits expose capabilities") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  // Amphibious creatures gained swim + breath_water capabilities.
  auto frog = dnd.createCreature("giant_frog");
  REQUIRE(frog != nullptr);
  CHECK(frog->hasTrait("amphibious"));
  CHECK(frog->hasCapability("swim"));
  CHECK(frog->hasCapability("breath_water"));
  CHECK_FALSE(frog->hasCapability("climb"));

  // Spider Climb grants climb.
  auto spider = dnd.createCreature("giant_spider");
  REQUIRE(spider != nullptr);
  CHECK(spider->hasTrait("spider_climb"));
  CHECK(spider->hasCapability("climb"));

  // Water Breathing grants breath_water.
  auto octopus = dnd.createCreature("giant_octopus");
  REQUIRE(octopus != nullptr);
  CHECK(octopus->hasCapability("breath_water"));
  CHECK_FALSE(octopus->hasCapability("swim")); // it does not "swim" in the trait sense

  // A non-amphibious creature has none of these.
  auto wolf = dnd.createCreature("wolf");
  REQUIRE(wolf != nullptr);
  CHECK_FALSE(wolf->hasCapability("swim"));
  CHECK_FALSE(wolf->hasCapability("breath_water"));
}

TEST_CASE("bookkeeping: shipped D&D conditions enforce action restrictions") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity hero(ruleset, "hero");
  hero.setBaseAttribute("WIS", 10);
  hero.loadFromArchetype(rpg_os::Json::object()); // initialise the MP pool

  // A healthy hero may act, cast, and move.
  CHECK(dnd.actionAllowed(hero, "action"));
  CHECK(dnd.actionAllowed(hero, "cast"));
  CHECK(dnd.actionAllowed(hero, "move"));

  // Incapacitated: no action / bonus action / reaction — but it can still move.
  dnd.applyCondition(hero, "incapacitated", 1, 0);
  CHECK_FALSE(dnd.actionAllowed(hero, "action"));
  CHECK_FALSE(dnd.actionAllowed(hero, "reaction"));
  CHECK(dnd.actionAllowed(hero, "move"));
  CHECK(hero.hasRestriction("no_action"));
  dnd.removeCondition(hero, "incapacitated");

  // Paralyzed: cannot act, cast, move, or speak.
  dnd.applyCondition(hero, "paralyzed", 1, 0);
  CHECK_FALSE(dnd.actionAllowed(hero, "action"));
  CHECK_FALSE(dnd.actionAllowed(hero, "cast"));
  CHECK_FALSE(dnd.actionAllowed(hero, "move"));
  CHECK_FALSE(dnd.actionAllowed(hero, "speak"));
  CHECK(hero.hasRestriction("no_concentration"));
  dnd.removeCondition(hero, "paralyzed");

  // Restrained: speed is 0, so it cannot move — but it can still act.
  dnd.applyCondition(hero, "restrained", 1, 0);
  CHECK_FALSE(dnd.actionAllowed(hero, "move"));
  CHECK(dnd.actionAllowed(hero, "action"));
  dnd.removeCondition(hero, "restrained");

  // The spell-resource ruleset has no spell_resource here, so verify the
  // restriction is enforced at cast time in the D&D ruleset's own terms:
  // a paralyzed caster's spell is refused and no resource is spent.
  dnd.applyCondition(hero, "paralyzed", 1, 0);
  auto rng = script({});
  // Paralyzed cannot take actions at all, so casting is refused for that
  // reason (no explicit no_cast needed — no_action implies it).
  const auto denied = dnd.castSpell("fireball", hero, nullptr, CheckParams{}, rng);
  CHECK_FALSE(denied.cast);
  CHECK(denied.denied == "no_action");
  CHECK(rng.idx == 0);
}
