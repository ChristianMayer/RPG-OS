// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_effects.cpp
 * @brief The effect timeline: durations, stat bonuses, recurring/ongoing effects, option groups.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: EffectTimeline ticks and expires (phase 0)") {
  rpg_os::EffectTimeline timeline;
  timeline.add(rpg_os::ActiveEffect{"fear", 2, 3, "spell"});
  timeline.add(rpg_os::ActiveEffect{"fear", 1, 3, "spell"});
  CHECK(timeline.stacks("fear") == 3);
  CHECK(timeline.tick() == 0);
  CHECK(timeline.tick() == 0);
  CHECK(timeline.tick() == 1); // expires now
  CHECK_FALSE(timeline.has("fear"));
  // permanent effects survive ticks
  timeline.add(rpg_os::ActiveEffect{"blessed", 1, 0, ""});
  CHECK(timeline.tick() == 0);
  CHECK(timeline.has("blessed"));
}

TEST_CASE("bookkeeping: stat bonuses tick, expire, and survive serialization (phase 0)") {
  rpg_os::EffectTimeline timeline;
  timeline.addBonus(rpg_os::StatBonus{"STR", 3, 2, "spell"});
  timeline.addBonus(rpg_os::StatBonus{"PER", 5, 0, ""});
  CHECK(timeline.bonusFor("STR") == 3);
  CHECK(timeline.bonusFor("PER") == 5);
  CHECK(timeline.bonusFor("DEX") == 0);

  // A permanent bonus survives ticks; the timed one ticks down.
  CHECK(timeline.tick() == 0); // STR 2 -> 1
  CHECK(timeline.bonusFor("STR") == 3);
  CHECK(timeline.bonusFor("PER") == 5);
  CHECK(timeline.tick() == 1); // STR 1 -> 0: expires
  CHECK(timeline.bonusFor("STR") == 0);
  CHECK(timeline.bonusFor("PER") == 5);

  // Serialization round-trip preserves the remaining bonuses.
  rpg_os::Json saved;
  timeline.toJson(saved);
  rpg_os::EffectTimeline restored;
  restored.fromJson(saved);
  CHECK(restored.bonusFor("PER") == 5);
  CHECK(restored.bonusFor("STR") == 0);
}

TEST_CASE("bookkeeping: stat_bonus effects apply a temporary stat modifier") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("STR", 12);
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());

  // A flat `add` raises the effective stat.
  const rpg_os::Json flat = rpg_os::Json::parse(R"json([
    {"kind": "stat_bonus", "stat": "STR", "add": 3}
  ])json");
  auto rng = script({});
  const auto result = dnd.resolveEffects(caster, target, flat, CheckParams{}, rng);
  CHECK(result.statBonusesApplied == 1);
  CHECK(target.getEffectiveStat("STR") == 15);

  // A QL-scaled `add` (The Dark Eye style buff) raises it further.
  const rpg_os::Json ql = rpg_os::Json::parse(R"json([
    {"kind": "stat_bonus", "stat": "STR", "add": "env.ql * 2"}
  ])json");
  auto rng2 = script({});
  (void)dnd.resolveEffects(caster, target, ql, CheckParams{}, rng2, 3);
  CHECK(target.getEffectiveStat("STR") == 21); // 12 + 3 + 6
}

TEST_CASE("bookkeeping: recurring effects re-apply each turn and expire") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 30);
  target.loadFromArchetype(rpg_os::Json::object());

  // 2d4 Acid now, then 2d4 again at the end of the target's next turn.
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "damage", "dice": "2d4", "type": "Acid",
     "ongoing": {"at": "end_of_turn", "duration": 1}}
  ])json");
  auto rng1 = script({1, 1}); // initial 2d4 = 2
  const auto first = dnd.resolveEffects(caster, target, effects, CheckParams{}, rng1);
  CHECK(first.damageDealt == 2);
  CHECK(target.resource("HP") == 28);

  // End of the target's turn: the recurring effect fires once more (2d4 = 2).
  auto rng2 = script({1, 1});
  dnd.processOngoing(target, "end_of_turn", rng2);
  CHECK(target.resource("HP") == 26);

  // Next turn: the recurring effect has expired; no further damage.
  auto rng3 = script({});
  dnd.processOngoing(target, "end_of_turn", rng3);
  CHECK(target.resource("HP") == 26);
}

TEST_CASE("bookkeeping: runTurn fires recurring effects at the declared phase") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 30);
  target.loadFromArchetype(rpg_os::Json::object());

  // A start-of-turn recurring heal (regeneration-style).
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "heal", "dice": "1d6", "ongoing": {"at": "start_of_turn", "duration": 1}}
  ])json");
  // Drop the target low first so heals have room to apply.
  (void)target.modifyResource("HP", -10);
  auto rng1 = script({2}); // initial heal 2
  const auto first = dnd.resolveEffects(caster, target, effects, CheckParams{}, rng1);
  CHECK(first.healingDone == 2);
  CHECK(target.resource("HP") == 22); // 20 + 2

  // A runTurn fires the start-of-turn recurring heal once more (4).
  (void)target.modifyResource("HP", -10); // down to 12
  auto rng2 = script({4});
  dnd.runTurn(target, rng2);
  CHECK(target.resource("HP") == 16); // 12 + 4

  // The second runTurn has no recurring heal left.
  auto rng3 = script({});
  dnd.runTurn(target, rng3);
  CHECK(target.resource("HP") == 16);
}

TEST_CASE("bookkeeping: ongoing effects can re-apply a different effect (acid arrow)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("INT_mod", 4);
  caster.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("AC", 15);
  target.setBaseAttribute("HitPoints_Max", 30);
  target.loadFromArchetype(rpg_os::Json::object());

  // 4d4 Acid on a hit, then 2d4 Acid at the end of the target's next turn.
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "damage", "dice": "4d4", "type": "Acid",
     "attack": {"bonus_stats": ["INT_mod", "proficiency_bonus"], "target_stat": "AC"},
     "ongoing": {"at": "end_of_turn", "duration": 1,
                 "effect": {"kind": "damage", "dice": "2d4", "type": "Acid"}}}
  ])json");
  // Hit: 4d4 all 1s = 4, then the attack d20 15 + 4 + 2 = 21 >= AC 15.
  auto rng1 = script({1, 1, 1, 1, 15});
  const auto hit = dnd.resolveEffects(caster, target, effects, CheckParams{}, rng1);
  CHECK(hit.damageDealt == 4);
  CHECK(target.resource("HP") == 26);
  // End of turn: the recurring 2d4 fires (all 1s = 2).
  auto rng2 = script({1, 1});
  dnd.processOngoing(target, "end_of_turn", rng2);
  CHECK(target.resource("HP") == 24);

  // A miss registers no recurring damage: fresh target, miss the attack.
  DynamicEntity target2(ruleset, "target2");
  target2.setBaseAttribute("AC", 15);
  target2.setBaseAttribute("HitPoints_Max", 30);
  target2.loadFromArchetype(rpg_os::Json::object());
  auto rng3 = script({1, 1, 1, 1, 5}); // 4d4 rolled, attack 5 + 6 = 11 < 15 miss
  const auto miss = dnd.resolveEffects(caster, target2, effects, CheckParams{}, rng3);
  CHECK(miss.damageDealt == 0);
  auto rng4 = script({});
  dnd.processOngoing(target2, "end_of_turn", rng4);
  CHECK(target2.resource("HP") == 30); // no recurring damage on a miss
}

TEST_CASE("bookkeeping: temp-hp effects grant temporary hit points (D&D false life)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());

  // false_life: 2d4 + 4 temporary hit points, no save.
  auto rng = script({1, 1}); // 2d4 all 1s = 2, +4 = 6
  const auto result = dnd.castSpell("false_life", caster, &target, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(target.temporaryHitPoints() == 6);
  CHECK(target.resource("HP") == 10); // real hit points untouched
}

TEST_CASE("bookkeeping: bonus_die effects add a die to matching checks") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity fighter(ruleset, "fighter");
  fighter.setBaseAttribute("STR_mod", 3);
  fighter.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity orc(ruleset, "orc");
  orc.setBaseAttribute("AC", 15);

  // Bless-like: a +1d4 bonus die on attack checks, applied to the roller.
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "bonus_die", "dice": "1d4", "scope": "attack", "duration": 0}
  ])json");
  auto rng0 = script({});
  (void)dnd.resolveEffects(caster, fighter, effects, CheckParams{}, rng0);
  CHECK(fighter.effects().bonusDice().size() == 1);

  // d20 8 + 3 + 2 = 13 would miss, but the bonus 1d4 rolls 4 -> 17 >= AC 15.
  auto rng = script({8, 4});
  const auto hit = dnd.executeCheck("dnd5e_attack_melee", fighter, &orc, CheckParams{}, rng);
  CHECK(hit.isSuccess);
  CHECK(hit.rawDiceRolls == std::vector<int>{8, 4});

  // The bonus die is scoped: a non-attack check does not roll it.
  DynamicEntity other(ruleset, "other");
  other.setBaseAttribute("STR_mod", 3);
  other.setBaseAttribute("proficiency_bonus", 2);
  CheckParams saveParams;
  saveParams.difficulty = 15;
  auto rng2 = script({8});
  const auto save = dnd.executeCheck("dnd5e_save_str", other, nullptr, saveParams, rng2);
  CHECK_FALSE(save.isSuccess); // 8 + 5 = 13 < DC 15
  CHECK(save.rawDiceRolls == std::vector<int>{8});
}

TEST_CASE("bookkeeping: quality-scaled damage effects add the QL formula (TDE style)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 30);
  target.loadFromArchetype(rpg_os::Json::object()); // initialise the HP pool

  // "2D6 + QL x 2": dice 2d6 with a formula `add` over the cast's QL (env.ql).
  const rpg_os::Json effects = rpg_os::Json::parse(R"([
    {"kind": "damage", "dice": "2d6", "type": "Arcane", "add": "env.ql * 2"}
  ])");
  auto rng = script({1, 1}); // 2d6 all 1s = 2
  const auto result = dnd.resolveEffects(caster, target, effects, CheckParams{}, rng, 4);
  CHECK(result.damageDealt == 2 + 8); // 2 + 4*2 = 10
  CHECK(target.resource("HP") == 20);

  // A QL of 1 scales down to the bare roll.
  DynamicEntity target2(ruleset, "target2");
  target2.setBaseAttribute("HitPoints_Max", 30);
  target2.loadFromArchetype(rpg_os::Json::object());
  auto rng2 = script({2, 2});
  const auto low = dnd.resolveEffects(caster, target2, effects, CheckParams{}, rng2, 1);
  CHECK(low.damageDealt == 4 + 2); // 4 + 1*2 = 6
  CHECK(target2.resource("HP") == 24);
}

TEST_CASE("bookkeeping: quality-scaled condition stacks resolve the QL formula (TDE style)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());

  // QL 4: "QL 3: 2 levels, QL 4: 3 levels" -> clamp(env.ql - 1, 1, 4) = 3.
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "condition", "condition": "cursed", "stacks": "clamp(env.ql - 1, 1, 4)"}
  ])json");
  auto rng = script({});
  const auto high = dnd.resolveEffects(caster, target, effects, CheckParams{}, rng, 4);
  CHECK(target.conditionStacks("cursed") == 3);
  CHECK(high.conditionsApplied == 1);

  // QL 1 resolves to the minimum of 1 stack.
  DynamicEntity target2(ruleset, "target2");
  auto rng2 = script({});
  (void)dnd.resolveEffects(caster, target2, effects, CheckParams{}, rng2, 1);
  CHECK(target2.conditionStacks("cursed") == 1);
}

TEST_CASE("bookkeeping: roll-under saves express TDE resistance (stat minus QL)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("CON", 12);
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());

  // TDE resistance: the target resists when it rolls d20 <= CON - QL.
  // QL 3 -> threshold 12 - 3 = 9.
  const rpg_os::Json effects = rpg_os::Json::parse(R"json([
    {"kind": "condition", "condition": "cursed", "stacks": 1,
     "save": {"stat": "CON", "comparison": "le", "dc": "-env.ql"}}
  ])json");
  // d20 5 <= 9 -> resisted; the condition does not apply.
  auto rngResist = script({5});
  const auto resisted = dnd.resolveEffects(caster, target, effects, CheckParams{}, rngResist, 3);
  CHECK_FALSE(target.hasCondition("cursed"));
  CHECK(resisted.savesPassed == 1);

  // d20 12 > 9 -> not resisted; the condition applies.
  auto rngFail = script({12});
  (void)dnd.resolveEffects(caster, target, effects, CheckParams{}, rngFail, 3);
  CHECK(target.hasCondition("cursed"));
}

TEST_CASE("bookkeeping: options effects require a caller choice (choice interface)") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "WIS_mod", "name": "Wisdom Mod", "formula": "floor((WIS - 10) / 2)"}],
    "resource_pools": [{"id": "HP", "name": "Hit Points", "max_stat": "STR", "min": 0}],
    "data": {
      "spells": [
        {"id": "elemental_blast", "name": "Elemental Blast", "level": 1, "effects": [
          {"kind": "options", "id": "element", "options": [
            {"kind": "damage", "dice": "1d6", "type": "Fire"},
            {"kind": "damage", "dice": "1d8", "type": "Cold"}
          ]}
        ]}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("STR", 10);
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("STR", 20);
  target.loadFromArchetype(rpg_os::Json::object()); // initialise the HP pool

  // No selection: the required option group is reported in choicesRequired
  // and nothing is applied (no dice are rolled).
  auto rng = script({});
  const auto pending = engine.castSpell("elemental_blast", caster, &target, CheckParams{}, rng);
  CHECK(pending.cast);
  CHECK(pending.choicesRequired == std::vector<std::string>{"element"});
  CHECK(pending.appliedDamage == 0);
  CHECK(target.resource("HP") == 20);
  CHECK(rng.idx == 0);

  // Selecting the cold option (index 1) resolves it: 1d8 = 4 damage.
  const std::unordered_map<std::string, int32_t> selections{{"element", 1}};
  auto rng2 = script({4});
  const auto chosen =
      engine.castSpell("elemental_blast", caster, &target, "", CheckParams{}, rng2, &selections);
  CHECK(chosen.cast);
  CHECK(chosen.choicesRequired.empty());
  CHECK(chosen.appliedDamage == 4);
  CHECK(target.resource("HP") == 16);
}

TEST_CASE("bookkeeping: optional option groups are skipped without a choice") {
  const std::string rulesetJson = R"json({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10},
                   {"id": "WIS", "name": "Wisdom", "min": 1, "max": 30, "default": 10}],
    "derived_stats": [{"id": "WIS_mod", "name": "Wisdom Mod", "formula": "floor((WIS - 10) / 2)"}],
    "resource_pools": [{"id": "HP", "name": "Hit Points", "max_stat": "STR", "min": 0}],
    "data": {
      "spells": [
        {"id": "versatile", "name": "Versatile", "level": 1, "effects": [
          {"kind": "options", "id": "mode", "optional": true, "options": [
            {"kind": "damage", "dice": "1d4", "type": "Fire"}
          ]}
        ]}
      ]
    }
  })json";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("STR", 10);
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("STR", 20);
  target.loadFromArchetype(rpg_os::Json::object());

  // No selection on an optional group is fine: nothing required, nothing applied.
  auto rng = script({});
  const auto skipped = engine.castSpell("versatile", caster, &target, CheckParams{}, rng);
  CHECK(skipped.cast);
  CHECK(skipped.choicesRequired.empty());
  CHECK(skipped.appliedDamage == 0);
  CHECK(target.resource("HP") == 20);

  // With a selection the chosen option resolves.
  const std::unordered_map<std::string, int32_t> selections{{"mode", 0}};
  auto rng2 = script({2});
  const auto chosen =
      engine.castSpell("versatile", caster, &target, "", CheckParams{}, rng2, &selections);
  CHECK(chosen.appliedDamage == 2);
  CHECK(target.resource("HP") == 18);
}
