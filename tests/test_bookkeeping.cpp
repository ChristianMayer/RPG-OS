// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_bookkeeping.cpp
 * @brief Tests for the universal bookkeeping layer.
 *
 * The library is the "operating system" of a tabletop RPG: it does the
 * bookkeeping for everything that can happen — what a character owns, wears,
 * owes, knows, has cast, has earned — without deciding what *should* happen.
 * These tests pin that layer across all phases: the core types (money,
 * inventory, equipment, effects, spellbook, advancement), the economy
 * (prices, buying, paying), equipment & encumbrance (including gear that
 * modifies effective stats), mechanical conditions with durations, magic
 * (prepared pool and vancian slot casting), advancement and rests, and
 * curses. Data-driven mechanics are exercised against inline mini-rulesets so
 * no source-book numbers need inventing.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/core/advancement.hpp>
#include <rpg_os/core/effects.hpp>
#include <rpg_os/core/equipment.hpp>
#include <rpg_os/core/inventory.hpp>
#include <rpg_os/core/money.hpp>
#include <rpg_os/core/spellbook.hpp>
#include <rpg_os/universal/engine.hpp>
#include <rpg_os/universal/game_session.hpp>
#include <string>

using rpg_os::BookkeepingError;
using rpg_os::CheckParams;
using rpg_os::CurrencySystem;
using rpg_os::DynamicEntity;
using rpg_os::ItemInstance;
using rpg_os::Money;
using rpg_os::RulesetEngine;

namespace {

std::string rulesetPath(const char *name) {
  return std::string(RPG_OS_SOURCE_DIR) + "/rulesets/" + name;
}

/// A minimal D&D-style coinage for unit tests.
CurrencySystem dndCoins() {
  return CurrencySystem{"dnd",
                        "D&D",
                        "cp",
                        {{"cp", "Copper", "CP", 1},
                         {"sp", "Silver", "SP", 10},
                         {"gp", "Gold", "GP", 100},
                         {"pp", "Platinum", "PP", 1000}}};
}

} // namespace

TEST_CASE("bookkeeping: Money stores base units and converts (phase 0)") {
  const CurrencySystem coins = dndCoins();
  const Money twoGold = Money::fromCoins(coins, {{"gp", 2}});
  CHECK(twoGold.baseUnits() == 200);
  const Money bag = Money::fromCoins(coins, {{"gp", 2}, {"sp", 5}, {"cp", 3}});
  CHECK(bag.baseUnits() == 253);
  CHECK(bag.in(coins, "gp") == 2);
  CHECK(bag.in(coins, "sp") == 25);
  CHECK((twoGold + bag).baseUnits() == 453);
  CHECK((bag - twoGold).baseUnits() == 53);
  CHECK((bag * 3).baseUnits() == 759);
  CHECK(twoGold.baseUnits() < bag.baseUnits());
  CHECK_FALSE(twoGold.baseUnits() > bag.baseUnits());
  CHECK_THROWS_AS(static_cast<void>(Money::fromCoins(coins, {{"nope", 1}})), std::invalid_argument);
  CHECK(coins.valueOf("gp", 3) == 300);
  CHECK(coins.valueOf("nope", 1) == 0);
}

TEST_CASE("bookkeeping: Inventory adds, merges, and removes (phase 0)") {
  rpg_os::Inventory inv;
  inv.add(ItemInstance{"sword", 1, {}});
  inv.add(ItemInstance{"sword", 2, {}});
  CHECK(inv.count("sword") == 3);
  inv.add(ItemInstance{"potion", 5, {}});
  CHECK(inv.size() == 2);
  CHECK(inv.remove("sword", 2));
  CHECK(inv.count("sword") == 1);
  CHECK_FALSE(inv.remove("sword", 5)); // not enough -> unchanged
  CHECK(inv.count("sword") == 1);
  CHECK(inv.remove("sword", 1));
  CHECK_FALSE(inv.has("sword"));
}

TEST_CASE("bookkeeping: Equipment slots hold one item each (phase 0)") {
  rpg_os::Equipment eq;
  CHECK(eq.equip("weapon_hand", "sword").empty());
  CHECK(eq.isEquipped("weapon_hand"));
  CHECK(eq.itemIn("weapon_hand") == "sword");
  CHECK(eq.equip("weapon_hand", "axe") == "sword"); // overwrite returns old
  CHECK(eq.itemIn("weapon_hand") == "axe");
  CHECK(eq.unequip("weapon_hand") == "axe");
  CHECK_FALSE(eq.isEquipped("weapon_hand"));
  CHECK(eq.unequip("weapon_hand").empty());
}

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

TEST_CASE("bookkeeping: itemPrice parses D&D cost strings (phase 1)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  REQUIRE(dnd.hasCurrency());
  const auto club = dnd.itemPrice("club"); // "1 SP"
  REQUIRE(club.has_value());
  CHECK(club->baseUnits() == 10); // 1 silver = 10 copper
  CHECK(dnd.itemPrice("nope").error() == BookkeepingError::UnknownItem);
}

TEST_CASE("bookkeeping: TDE prices use Silbertaler (phase 1)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto dagger = tde.itemPrice("dagger"); // "45 ST"
  REQUIRE(dagger.has_value());
  CHECK(dagger->baseUnits() == 4500); // 45 * 100 heller
  CHECK(dagger->in(tde.currencySystem(), "silbertaler") == 45);
  CHECK(dagger->in(tde.currencySystem(), "dukat") == 22); // 4500 / 200
  // a ruleset without money has no prices
  RulesetEngine brp;
  REQUIRE(brp.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  CHECK_FALSE(brp.hasCurrency());
  CHECK(brp.itemPrice("dagger").error() == BookkeepingError::UnknownCurrency);
}

TEST_CASE("bookkeeping: buy and pay transfer money and items (phase 1)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  CHECK(geron->money().baseUnits() == 2500); // 25 ST starting wealth
  geron->money() = Money{50000};

  const auto total = tde.buy(*geron, nullptr, "dagger", 2);
  REQUIRE(total.has_value());
  CHECK(total->baseUnits() == 9000); // 2 * 45 ST
  CHECK(geron->money().baseUnits() == 50000 - 9000);
  CHECK(geron->inventory().count("dagger") == 2);

  // insufficient funds fail without side effects
  geron->money() = Money{10};
  const auto poor = tde.buy(*geron, nullptr, "dagger", 1);
  CHECK(poor.error() == BookkeepingError::NotEnoughMoney);
  CHECK(geron->inventory().count("dagger") == 2);
  CHECK(geron->money().baseUnits() == 10);

  // pay transfers between two sheets
  geron->money() = Money{1000};
  auto other = tde.createEntity("geron");
  REQUIRE(other != nullptr);
  REQUIRE(tde.pay(*geron, other.get(), Money{400}).has_value());
  CHECK(geron->money().baseUnits() == 600);
  CHECK(other->money().baseUnits() == 2500 + 400);
  CHECK(tde.pay(*geron, nullptr, Money{100000}).error() == BookkeepingError::NotEnoughMoney);
}

TEST_CASE("bookkeeping: equip and unequip move items between slots (phase 2)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto sheet = tde.createEntity("geron");
  REQUIRE(sheet != nullptr);
  // geron starts with a longsword equipped
  CHECK(sheet->equipment().isEquipped("weapon_hand"));
  CHECK(sheet->equipment().itemIn("weapon_hand") == "longsword");

  // cannot equip what you do not own
  CHECK(tde.equip(*sheet, "weapon_hand", "dagger").error() == BookkeepingError::ItemNotOwned);

  REQUIRE(tde.addItem(*sheet, "dagger", 1).has_value());
  const auto equipped = tde.equip(*sheet, "weapon_hand", "dagger");
  REQUIRE(equipped.has_value());
  CHECK(equipped->previousItemId == "longsword"); // swap
  CHECK(sheet->equipment().itemIn("weapon_hand") == "dagger");
  CHECK(sheet->inventory().count("longsword") == 1); // old item returned
  CHECK(sheet->inventory().count("dagger") == 0);

  // unequip returns the item
  REQUIRE(tde.unequip(*sheet, "weapon_hand").has_value());
  CHECK_FALSE(sheet->equipment().isEquipped("weapon_hand"));
  CHECK(sheet->inventory().count("dagger") == 1);

  // slot / item mismatches fail
  CHECK(tde.equip(*sheet, "weapon_hand", "leather_armor").error() ==
        BookkeepingError::SlotMismatch);
  CHECK(tde.equip(*sheet, "no_such_slot", "dagger").error() == BookkeepingError::SlotMismatch);
  CHECK(tde.unequip(*sheet, "weapon_hand").error() == BookkeepingError::NotEquipped);
}

TEST_CASE("bookkeeping: equipped gear modifies effective stats (phase 2)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto sheet = tde.createEntity("geron");
  REQUIRE(sheet != nullptr);
  // geron base Armor_Rating = 2; leather armor adds +3
  CHECK(sheet->baseAttribute("Armor_Rating") == 2);
  CHECK(sheet->getStat("Armor_Rating") == 2); // raw stays raw
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 5);

  REQUIRE(tde.unequip(*sheet, "body_armor").has_value());
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 2);
  REQUIRE(tde.equip(*sheet, "body_armor", "leather_armor").has_value());
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 5);
}

TEST_CASE("bookkeeping: D&D encumbrance from weight and capacity (phase 2)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity sheet(ruleset, "test");
  sheet.setBaseAttribute("STR", 10); // capacity = 15 * 10 = 150 lb

  REQUIRE(dnd.addItem(sheet, "club", 10).has_value()); // 10 * 2 lb = 20 lb
  CHECK(dnd.carriedWeight(sheet).value == doctest::Approx(20.0));
  const auto capacity = dnd.carryingCapacity(sheet);
  REQUIRE(capacity.has_value());
  CHECK(capacity->value == doctest::Approx(150.0));
  const auto level = dnd.encumbranceLevel(sheet);
  REQUIRE(level.has_value());
  CHECK(*level == 0);

  REQUIRE(dnd.addItem(sheet, "club", 70).has_value()); // 160 lb total
  const auto heavy = dnd.encumbranceLevel(sheet);
  REQUIRE(heavy.has_value());
  CHECK(*heavy == 2); // ratio 160/150 > 1.0 -> heaviest level
}

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

TEST_CASE("bookkeeping: gainXp levels through the xp_to_level table (phase 5)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  auto sheet = dnd.createCreature("goblin_warrior");
  REQUIRE(sheet != nullptr);
  CHECK(sheet->advancement().level == 1);

  const auto first = dnd.gainXp(*sheet, 299);
  REQUIRE(first.has_value());
  CHECK_FALSE(first->leveled);
  CHECK(sheet->advancement().level == 1);

  const auto second = dnd.gainXp(*sheet, 1); // total 300
  REQUIRE(second.has_value());
  CHECK(second->leveled);
  CHECK(second->toLevel == 2);
  CHECK(sheet->advancement().level == 2);
  CHECK(sheet->advancement().xp() == 300);
}

TEST_CASE("bookkeeping: improvementCost reads TDE AP columns (phase 5)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto cost = tde.improvementCost("AP_Column_C", 1);
  REQUIRE(cost.has_value());
  CHECK(*cost == 3);
  CHECK(tde.improvementCost("no_such_table", 1).error() == BookkeepingError::UnknownCostTable);
}

TEST_CASE("bookkeeping: a long rest restores resources and ends timed conditions (phase 5)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  const int32_t maxLp = geron->resource("LP");
  (void)geron->modifyResource("LP", -10);
  tde.applyCondition(*geron, "pain", 1, 1);
  CHECK(geron->resource("LP") < maxLp);
  CHECK(geron->hasCondition("pain"));

  tde.longRest(*geron);
  CHECK(geron->resource("LP") == maxLp);
  CHECK_FALSE(geron->hasCondition("pain")); // the rest's tick expired it
}

TEST_CASE("bookkeeping: curses apply and can be cured (phase 6)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "data": {
      "conditions": [{"id": "cursed", "name": "Cursed"}],
      "curses": [{
        "id": "wolf_curse", "name": "Wolf Curse",
        "effects": [{"stat": "condition", "amount": 1, "condition": "cursed"}]
      }]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity victim(ruleset, "v");

  auto rng = script({});
  const auto result = engine.applyCurse("wolf_curse", victim, CheckParams{}, rng);
  CHECK(result.effectsApplied == 1);
  CHECK(victim.hasCondition("cursed"));
  REQUIRE(victim.afflictions().size() == 1);

  CHECK(engine.cureAffliction(victim, "curses", "wolf_curse"));
  CHECK_FALSE(victim.hasCondition("cursed"));
  CHECK(victim.afflictions().empty());
  CHECK_FALSE(engine.cureAffliction(victim, "curses", "wolf_curse"));
}

TEST_CASE("bookkeeping: sheet serialization round-trips the whole state") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto original = tde.createEntity("geron");
  REQUIRE(original != nullptr);
  REQUIRE(tde.addItem(*original, "dagger", 2).has_value());
  tde.applyCondition(*original, "pain", 1, 3);
  original->advancement().gainXp(1000);
  original->spellbook().learn("balsam_salabunde");

  rpg_os::Json saved;
  original->toJson(saved);
  DynamicEntity restored(tde.ruleset(), "geron");
  restored.fromJson(saved);

  CHECK(restored.getStat("COU") == 12);
  CHECK(restored.money().baseUnits() == original->money().baseUnits());
  CHECK(restored.inventory().count("dagger") == 2);
  CHECK(restored.equipment().itemIn("weapon_hand") == "longsword");
  CHECK(restored.hasCondition("pain"));
  CHECK(restored.advancement().xp() == 1000);
  CHECK(restored.spellbook().knows("balsam_salabunde"));
  CHECK(restored.getEffectiveStat("Armor_Rating") == 5); // gear survives too
}

TEST_CASE("bookkeeping: GameSession facade owns sheets, world, and events") {
  rpg_os::GameSession session;
  REQUIRE(session.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto &geron = session.createCharacter("geron");
  CHECK(geron.money().baseUnits() == 2500);

  session.world().addToTreasury(Money{500});
  CHECK(session.world().treasury.baseUnits() == 500);
  session.world().advanceDays(3);
  CHECK(session.world().day == 3);

  int currencyEvents = 0;
  const uint64_t listener = session.onEvent(rpg_os::EventType::OnCurrencyChanged,
                                            [&](const rpg_os::EventData &) { ++currencyEvents; });
  REQUIRE(session.pay(geron, nullptr, Money{100}).has_value());
  CHECK(currencyEvents == 1);
  CHECK(session.engine().unregisterEventListener(listener));

  const auto leveled = session.gainXp(geron, 300); // TDE has no xp_to_level
  REQUIRE(leveled.has_value());
  CHECK_FALSE(leveled->leveled);
  CHECK(geron.advancement().xp() == 300);
}

TEST_CASE("bookkeeping: containers hold items (bag-in-bags)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "encumbrance": {"weight_unit": "lb", "default_weight": 0.0, "capacity": "15 * STR", "levels": []},
    "data": {
      "items": [
        {"id": "backpack", "name": "Backpack", "weight": "5 lb."},
        {"id": "club", "name": "Club", "weight": "2 lb."}
      ]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity sheet(ruleset, "test");
  sheet.setBaseAttribute("STR", 10);

  REQUIRE(engine.addItem(sheet, "backpack", 1).has_value());
  REQUIRE(engine.addItem(sheet, "club", 2).has_value());
  // flat count does not see what is inside the container...
  CHECK(sheet.inventory().count("club") == 2);
  CHECK(sheet.inventory().totalCount("club") == 2);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(9.0)); // 5 + 2*2

  // ...until we put a club into the backpack.
  REQUIRE(engine.addItemToContainer(sheet, "backpack", "club", 1).has_value());
  CHECK(sheet.inventory().countIn("backpack", "club") == 1);
  CHECK(sheet.inventory().totalCount("club") == 3);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(11.0)); // +2 in the pack
  REQUIRE(sheet.inventory().contentsOf("backpack") != nullptr);
  CHECK(sheet.inventory().contentsOf("backpack")->size() == 1);

  // a missing container is a distinct error
  CHECK(engine.addItemToContainer(sheet, "nope", "club", 1).error() ==
        BookkeepingError::UnknownContainer);

  // removing from the container returns the count to flat-only
  REQUIRE(engine.removeItemFromContainer(sheet, "backpack", "club", 1).has_value());
  CHECK(sheet.inventory().countIn("backpack", "club") == 0);
  CHECK(sheet.inventory().totalCount("club") == 2);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(9.0));

  // serialization round-trips nested contents
  REQUIRE(engine.addItemToContainer(sheet, "backpack", "club", 1).has_value());
  rpg_os::Json saved;
  sheet.toJson(saved);
  DynamicEntity restored(ruleset, "test");
  restored.fromJson(saved);
  CHECK(restored.inventory().countIn("backpack", "club") == 1);
  CHECK(restored.inventory().totalCount("club") == 3);
}

TEST_CASE("bookkeeping: effective armour reduces damage in the pipeline") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  rpg_os::DynamicEntity orc(tde.ruleset(), "orc");

  // geron wears leather armour: raw Armor_Rating 2, effective 5.
  CHECK(geron->baseAttribute("Armor_Rating") == 2);
  CHECK(geron->getEffectiveStat("Armor_Rating") == 5);
  const int32_t lpBefore = geron->resource("LP");
  CHECK(tde.applyDamage(orc, *geron, "LP", 10) == -5); // 10 - effective 5

  // without the armour only the raw rating applies
  REQUIRE(tde.unequip(*geron, "body_armor").has_value());
  const int32_t lpAfter = geron->resource("LP");
  CHECK(tde.applyDamage(orc, *geron, "LP", 10) == -8); // 10 - raw 2
  CHECK(geron->resource("LP") == lpBefore - 5 - 8);
  CHECK(geron->resource("LP") == lpAfter - 8);
}

TEST_CASE("bookkeeping: event triggers can grant items, money, and XP") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "resource_pools": [{"id": "HP", "name": "Hit Points", "max_stat": "STR", "min": 0}],
    "event_triggers": [
      {"id": "loot", "trigger": "on_damage_taken", "actions": [
        {"type": "gain_item", "item": "gold_coin", "amount": 1},
        {"type": "gain_currency", "amount": 5},
        {"type": "gain_xp", "amount": 10}
      ]},
      {"id": "fee", "trigger": "on_damage_calculated", "actions": [
        {"type": "spend_currency", "amount": 2}
      ]}
    ],
    "data": {
      "items": [{"id": "gold_coin", "name": "Gold Coin"}]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity victim(ruleset, "v");
  victim.setBaseAttribute("STR", 10);
  victim.loadFromArchetype(rpg_os::Json::object()); // initialise the HP pool
  DynamicEntity orc(ruleset, "orc");

  const int32_t hpBefore = victim.resource("HP");
  CHECK(engine.applyDamage(orc, victim, "HP", 10) == -10);
  CHECK(victim.resource("HP") == hpBefore - 10);
  CHECK(victim.inventory().count("gold_coin") == 1); // gained on hit
  CHECK(victim.money().baseUnits() == 3);            // +5 - 2 fee
  CHECK(victim.advancement().xp() == 10);            // gained on hit
}

TEST_CASE("bookkeeping: unknown event action types are rejected at load") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength"}],
    "event_triggers": [
      {"id": "bad", "trigger": "on_damage_taken", "actions": [{"type": "no_such_action"}]}
    ]
  })";
  RulesetEngine engine;
  CHECK_FALSE(engine.loadRulesetFromJson(rulesetJson));
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
