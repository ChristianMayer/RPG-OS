// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_game_session.cpp
 * @brief The GameSession facade: sheets, world state, events, save/load.
 */
#include "test_fixtures.hpp"

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

TEST_CASE("bookkeeping: advanceTime ticks durations and fires OnTimePassed") {
  rpg_os::GameSession session;
  REQUIRE(session.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto &geron = session.createCharacter("geron");

  // A short-lived condition decays once per day.
  session.applyCondition(geron, "pain", 1, 2);
  CHECK(geron.conditionStacks("pain") == 1);

  int timeEvents = 0;
  int32_t lastDay = -1;
  (void)session.onEvent(rpg_os::EventType::OnTimePassed, [&](const rpg_os::EventData &data) {
    ++timeEvents;
    lastDay = data.getInt("day");
  });

  const int32_t newDay = session.advanceTime(1);
  CHECK(newDay == 1);
  CHECK(session.world().day == 1);
  CHECK(timeEvents == 1);
  CHECK(lastDay == 1);

  // Second day: the 2-tick pain condition expires and is stripped.
  session.advanceTime(1);
  CHECK(timeEvents == 2);
  CHECK_FALSE(geron.hasCondition("pain"));
  CHECK(geron.conditionStacks("pain") == 0);
}

TEST_CASE("bookkeeping: GameSession save/load round-trips living state") {
  rpg_os::GameSession session;
  REQUIRE(session.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto &geron = session.createCharacter("geron");
  const rpg_os::EntityId geronId = geron.entityId();

  // Put the character into a non-default living state: spent pool, a timed
  // condition, and extra inventory.
  CHECK(geron.modifyResource("LP", -5) == -5);
  session.applyCondition(geron, "pain", 1, 3);
  REQUIRE(session.engine().addItem(geron, "dagger", 2).has_value());
  session.world().addToTreasury(Money{500});
  const int32_t lpAfterDamage = geron.resource("LP");
  const int32_t painStacks = geron.conditionStacks("pain");

  const rpg_os::Json save = session.saveState();
  CHECK(save.contains("sheets"));
  CHECK(save.at("sheets").size() == 1);
  CHECK(save.at("sheets")[0]["entity_id"] == geronId.toUint64());

  // Loading over the same session replaces the sheet and restores living state
  // under the original instance id.
  CHECK(session.loadState(save) == 1);
  CHECK(session.entityCount() == 1);
  rpg_os::DynamicEntity *restored = session.findEntity(geronId);
  REQUIRE(restored != nullptr);
  CHECK(restored->resource("LP") == lpAfterDamage);
  CHECK(restored->conditionStacks("pain") == painStacks);
  CHECK(restored->inventory().count("dagger") == 2);
  bool painHasDuration = false;
  for (const auto &effect : restored->effects().effects()) {
    if (effect.conditionId == "pain" && effect.remaining == 3) {
      painHasDuration = true;
    }
  }
  CHECK(painHasDuration);
  CHECK(session.world().treasury.baseUnits() == 500);
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
