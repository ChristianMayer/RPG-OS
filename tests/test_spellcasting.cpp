// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_spellcasting.cpp
 * @brief Spellbooks and casting: prepared pools, vancian slots, structured spell effects.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: Spellbook knows, prepares, and tracks slots (phase 0)") {
  rpg_os::Spellbook book;
  book.learn("fireball");
  CHECK(book.knows("fireball"));
  CHECK(book.prepare("fireball"));
  CHECK(book.hasPrepared("fireball"));
  CHECK_FALSE(book.prepare("unknown"));
  book.markSlotUsed(2);
  CHECK(book.slotsUsed(2) == 1);
  book.recoverAllSlots();
  CHECK(book.slotsUsed(2) == 0);
  book.unprepare("fireball");
  CHECK_FALSE(book.hasPrepared("fireball"));
  CHECK(book.forget("fireball"));
  CHECK_FALSE(book.knows("fireball"));
}

TEST_CASE("bookkeeping: prepareSpell reports an unknown spell via the return value") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  // prepareSpell is query-style: an unknown spell is an expected answer (unlike
  // castSpell, which throws because a cast must exist to resolve).
  CHECK_FALSE(tde.prepareSpell(*geron, "no_such_spell"));
  CHECK_FALSE(geron->spellbook().knows("no_such_spell"));
}

TEST_CASE("bookkeeping: prepared pool casting spends the resource (phase 4)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  REQUIRE(tde.prepareSpell(*geron, "balsam_salabunde"));
  CHECK(geron->spellbook().hasPrepared("balsam_salabunde"));

  const int32_t ae = geron->resource("AE");
  auto rng = script({10, 10, 10});
  const auto result =
      tde.castSpellPrepared("balsam_salabunde", *geron, nullptr, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(geron->resource("AE") == ae - 1);

  // an unprepared spell is never cast and costs nothing
  auto other = tde.createEntity("geron");
  REQUIRE(other != nullptr);
  auto rng2 = script({});
  const auto notPrepared =
      tde.castSpellPrepared("balsam_salabunde", *other, nullptr, CheckParams{}, rng2);
  CHECK_FALSE(notPrepared.cast);
}

TEST_CASE("bookkeeping: vancian casting spends a spell slot (phase 4)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "INT", "name": "Intellect", "min": 1, "max": 30, "default": 10}],
    "resource_pools": [{"id": "HP", "name": "Hit Points", "max_stat": "INT", "min": 0}],
    "spellcasting": {"style": "slots", "slots": {"2": 1}},
    "data": {
      "spells": [{"id": "magic_missile", "name": "Magic Missile", "level": 2, "damage": "1d4"}]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity sheet(ruleset, "test");
  REQUIRE(engine.prepareSpell(sheet, "magic_missile"));
  CHECK(engine.spellSlotsRemaining(sheet, 2) == 1);

  auto rng = script({3});
  const auto first = engine.castSpellPrepared("magic_missile", sheet, nullptr, CheckParams{}, rng);
  CHECK(first.cast);
  CHECK(engine.spellSlotsRemaining(sheet, 2) == 0);

  auto rng2 = script({});
  const auto second =
      engine.castSpellPrepared("magic_missile", sheet, nullptr, CheckParams{}, rng2);
  CHECK_FALSE(second.cast);

  // a long rest recovers the slot
  engine.longRest(sheet);
  CHECK(engine.spellSlotsRemaining(sheet, 2) == 1);
}

TEST_CASE("bookkeeping: structured spell effects resolve saves and damage (D&D fireball)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  // fireball's extracted save DC uses the caster's spellcasting ability
  // (first listed class Sorcerer -> CHA): 8 + proficiency_bonus + mod(16) = 13.
  caster.setBaseAttribute("CHA", 16);
  caster.setBaseAttribute("proficiency_bonus", 2);
  caster.setBaseAttribute("DEX", 8); // a low DEX must not lower the DC
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("DEX", 10);
  target.setBaseAttribute("HitPoints_Max", 30);
  target.loadFromArchetype(rpg_os::Json::object()); // initialise the HP pool

  // Save passed (d20 5 + DEX 10 = 15 >= 13): 8d6 all 1s = 8, halved to 4.
  auto rngSave = script({1, 1, 1, 1, 1, 1, 1, 1, 5});
  const auto result1 = dnd.castSpell("fireball", caster, &target, CheckParams{}, rngSave);
  CHECK(result1.cast);
  CHECK(result1.appliedDamage == 4);
  CHECK(target.resource("HP") == 26);

  // Save failed (d20 2 + DEX 10 = 12 < 13): full 8d6 all 2s = 16.
  auto rngFail = script({2, 2, 2, 2, 2, 2, 2, 2, 2});
  const auto result2 = dnd.castSpell("fireball", caster, &target, CheckParams{}, rngFail);
  CHECK(result2.appliedDamage == 16);
  CHECK(target.resource("HP") == 10);

  // The DC uses the caster's CHA, not the target's DEX: a target with DEX 8
  // rolling d20 4 (12) still fails the 13 DC, so it takes full damage.
  DynamicEntity weak(ruleset, "weak");
  weak.setBaseAttribute("DEX", 8);
  weak.setBaseAttribute("HitPoints_Max", 30);
  weak.loadFromArchetype(rpg_os::Json::object());
  auto rngWeak = script({1, 1, 1, 1, 1, 1, 1, 1, 4});
  const auto result3 = dnd.castSpell("fireball", caster, &weak, CheckParams{}, rngWeak);
  CHECK(result3.appliedDamage == 8);
}

TEST_CASE(
    "bookkeeping: structured spell effects apply conditions on a failed save (bestow curse)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  // bestow_curse's save DC also uses the caster's ability (Bard -> CHA), 13.
  caster.setBaseAttribute("CHA", 16);
  caster.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity victim(ruleset, "victim");
  victim.setBaseAttribute("WIS", 8); // needs d20 >= 5 to save
  victim.setBaseAttribute("HitPoints_Max", 10);
  victim.loadFromArchetype(rpg_os::Json::object());

  // Failed save (d20 3 + 8 = 11 < 13): the target is cursed.
  auto rngFail = script({3});
  const auto hit = dnd.castSpell("bestow_curse", caster, &victim, CheckParams{}, rngFail);
  CHECK(hit.cast);
  CHECK(victim.hasCondition("cursed"));
  CHECK(victim.conditionStacks("cursed") == 1);

  // Successful save (d20 6 + 8 = 14 >= 13): no additional curse stack.
  auto rngSave = script({6});
  const auto saved = dnd.castSpell("bestow_curse", caster, &victim, CheckParams{}, rngSave);
  CHECK(saved.cast);
  CHECK(victim.conditionStacks("cursed") == 1);
}

TEST_CASE("bookkeeping: structured heal effects restore hit points (D&D cure wounds)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  // cure_wounds' add is the caster's ability modifier (Bard -> CHA): mod(16)=3.
  caster.setBaseAttribute("CHA", 16);
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());
  (void)target.modifyResource("HP", -5); // drop to 5

  // 2d8 all 2s = 4, plus 3 = 7 -> clamps to the 10 maximum.
  auto rng = script({2, 2});
  const auto result = dnd.castSpell("cure_wounds", caster, &target, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(target.resource("HP") == 10);
}

TEST_CASE("bookkeeping: spell attack effects roll vs Armor Class (D&D fire bolt)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  // fire_bolt's attack bonus is the caster's ability (Sorcerer -> CHA) mod
  // plus proficiency: d20 + 3 + 2 vs the target's AC.
  caster.setBaseAttribute("CHA", 16);
  caster.setBaseAttribute("proficiency_bonus", 2);
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("AC", 13);
  target.setBaseAttribute("HitPoints_Max", 20);
  target.loadFromArchetype(rpg_os::Json::object());

  // Hit (d20 8 + 5 = 13 >= AC 13): 1d10 = 6 damage.
  auto rngHit = script({6, 8}); // damage dice first, then the attack d20
  const auto hit = dnd.castSpell("fire_bolt", caster, &target, CheckParams{}, rngHit);
  CHECK(hit.cast);
  CHECK(hit.appliedDamage == 6);
  CHECK(target.resource("HP") == 14);

  // Miss (d20 4 + 5 = 9 < AC 13): no damage.
  auto rngMiss = script({6, 4});
  const auto miss = dnd.castSpell("fire_bolt", caster, &target, CheckParams{}, rngMiss);
  CHECK(miss.cast);
  CHECK(miss.appliedDamage == 0);
  CHECK(target.resource("HP") == 14);
}

TEST_CASE("bookkeeping: structured resist effects grant resistance (D&D protection from poison)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity caster(ruleset, "caster");
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("HitPoints_Max", 10);
  target.loadFromArchetype(rpg_os::Json::object());

  auto rng = script({});
  const auto result = dnd.castSpell("protection_from_poison", caster, &target, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(target.hasResistance("Poison"));
}

TEST_CASE("bookkeeping: resolveEffects applies resistances and conditions (magic-item style)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity holder(ruleset, "holder");
  const rpg_os::Json effects = rpg_os::Json::parse(R"([
    {"kind": "resist", "types": ["Ranged", "Cold"]},
    {"kind": "condition", "condition": "cursed", "stacks": 1}
  ])");

  auto rng = script({});
  const auto result = dnd.resolveEffects(holder, holder, effects, CheckParams{}, rng);
  CHECK(holder.hasResistance("Ranged"));
  CHECK(holder.hasResistance("Cold"));
  CHECK(holder.hasCondition("cursed"));
  CHECK(result.conditionsApplied == 1);
  CHECK(result.savesPassed == 0);

  // resistances survive serialization
  rpg_os::Json saved;
  holder.toJson(saved);
  DynamicEntity restored(ruleset, "holder");
  restored.fromJson(saved);
  CHECK(restored.hasResistance("Ranged"));
}

TEST_CASE("bookkeeping: TDE spell effects resolve through castSpell with the cast QL") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto &ruleset = tde.ruleset();
  DynamicEntity caster(ruleset, "caster");
  // High casting attributes so any 3d20 spell check cannot overshoot.
  caster.setBaseAttribute("SGC", 15);
  caster.setBaseAttribute("INT", 15);
  caster.setBaseAttribute("CON", 15);
  caster.setBaseAttribute("COU", 15);
  caster.setBaseAttribute("CHA", 15);
  caster.loadFromArchetype(rpg_os::Json::object()); // AE pool = 20 + INT = 35
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("CON", 12); // LifePoints_Max = 5 + 2*12 = 29
  target.loadFromArchetype(rpg_os::Json::object());

  // fulminictus: cast pool check rolls 3d20 (10,10,10 all pass -> QL 1), then
  // damage 2d6 (1,1 = 2) + env.ql * 2 (QL 1 -> 2) = 4; LP 29 -> 25.
  auto rng = script({10, 10, 10, 1, 1});
  const auto result = tde.castSpell("fulminictus", caster, &target, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(result.appliedDamage == 4);
  CHECK(target.resource("LP") == 25);

  // blinding_flash: cast pool check (10,10,10), then the target resists the
  // Confusion with Spirit (roll-under Spirit minus QL 1). A roll of 3 <= 11
  // resists -> no condition; a roll above applies it.
  DynamicEntity victim(ruleset, "victim");
  victim.setBaseAttribute("Spirit", 12);
  victim.loadFromArchetype(rpg_os::Json::object());
  auto rngResist = script({10, 10, 10, 3});
  const auto saved = tde.castSpell("blinding_flash", caster, &victim, CheckParams{}, rngResist);
  CHECK(saved.cast);
  CHECK_FALSE(victim.hasCondition("confusion"));

  DynamicEntity victim2(ruleset, "victim2");
  victim2.setBaseAttribute("Spirit", 12);
  victim2.loadFromArchetype(rpg_os::Json::object());
  auto rngFail = script({10, 10, 10, 12});
  const auto hit = tde.castSpell("blinding_flash", caster, &victim2, CheckParams{}, rngFail);
  CHECK(hit.cast);
  CHECK(victim2.hasCondition("confusion"));
  CHECK(victim2.conditionStacks("confusion") == 1);
}

TEST_CASE("bookkeeping: a failed spell casting check fizzles — no damage is applied") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto &ruleset = tde.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("SGC", 15);
  caster.setBaseAttribute("INT", 15);
  caster.setBaseAttribute("CON", 15);
  caster.loadFromArchetype(rpg_os::Json::object()); // AE pool = 20 + INT = 35
  DynamicEntity target(ruleset, "target");
  target.setBaseAttribute("CON", 12); // LifePoints_Max = 5 + 2*12 = 29
  target.loadFromArchetype(rpg_os::Json::object());

  // fulminictus with a scripted casting check that fails: every 3d20 die
  // (16) exceeds its attribute (15), so the cast fizzles — the action and the
  // AE are spent, but no damage dice are even rolled and the target is
  // untouched. This pins the fight-log invariant: a fizzle changes no stat.
  auto rng = script({16, 16, 16});
  const auto result = tde.castSpell("fulminictus", caster, &target, CheckParams{}, rng);
  CHECK_FALSE(result.cast);
  CHECK(result.appliedDamage == 0);
  CHECK(target.resource("LP") == 29); // untouched
  CHECK(rng.idx == 3);                // only the check dice were rolled — the damage was skipped
}

TEST_CASE("bookkeeping: TDE buff spells apply stat bonuses via castSpell") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto &ruleset = tde.ruleset();
  DynamicEntity caster(ruleset, "caster");
  caster.setBaseAttribute("SGC", 15);
  caster.setBaseAttribute("INT", 15);
  caster.setBaseAttribute("DEX", 15);
  caster.setBaseAttribute("COU", 15);
  caster.setBaseAttribute("CHA", 15);
  caster.loadFromArchetype(rpg_os::Json::object());
  DynamicEntity ally(ruleset, "ally");
  ally.setBaseAttribute("perception", 5);
  ally.loadFromArchetype(rpg_os::Json::object());

  // eagle_eye: cast pool check 3d20 (10,10,10 pass -> QL 1), then the
  // stat_bonus effect adds env.ql + 3 = 4 to Perception.
  auto rng = script({10, 10, 10});
  const auto result = tde.castSpell("eagle_eye", caster, &ally, CheckParams{}, rng);
  CHECK(result.cast);
  CHECK(ally.getEffectiveStat("perception") == 9); // 5 + 4
  // The bonus is a temporary effect: it appears in the sheet's timeline and
  // survives serialization.
  CHECK(ally.effects().bonusFor("perception") == 4);
  rpg_os::Json saved;
  ally.toJson(saved);
  DynamicEntity restored(tde.ruleset(), "ally");
  restored.fromJson(saved);
  CHECK(restored.getEffectiveStat("perception") == 9);
}
