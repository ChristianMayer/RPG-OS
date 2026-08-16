// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_combat_session.cpp
 * @brief Tests for the turn & action-economy orchestrator (@c rpg_os::CombatSession).
 *
 * The code review's gap was "no turn & action-economy orchestrator": the
 * engine could tick one sheet's durations, but nothing owned an initiative
 * order, per-turn action budgets, or round-based duration decay across a
 * whole combat. These tests pin the new orchestrator: initiative ordering,
 * the per-turn @c ActionBudget (and its enforcement alongside condition
 * restrictions), and — the headline — that a timed condition decays to
 * nothing across the affected combatant's turns when the session drives
 * @c RulesetEngine::runTurn per combatant per round.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/universal/combat_session.hpp>
#include <rpg_os/universal/game_session.hpp>
#include <string>

using rpg_os::ActionKind;
using rpg_os::CombatSession;
using rpg_os::EntityId;
using rpg_os::GameSession;

namespace {

/// A minimal two-attribute ruleset with an Initiative derived stat, one
/// archetype, and a "paralyzed" condition that forbids every action kind.
constexpr std::string_view kMini = R"json({
  "schema_version": 1,
  "ruleset_id": "combat_demo",
  "ruleset_name": "Combat Demo",
  "licence": "test",
  "namespace": "rpg_os::generated::combat_demo",
  "attributes": [
    { "id": "COU", "name": "Courage", "min": 1, "max": 21, "default": 10 },
    { "id": "AGI", "name": "Agility", "min": 1, "max": 21, "default": 10 }
  ],
  "derived_stats": [
    { "id": "Initiative", "name": "Initiative", "formula": "round((COU + AGI) / 2)" }
  ],
  "data": {
    "archetypes": [
      { "id": "geron", "name": "Geron", "attributes": { "COU": 12, "AGI": 13 } }
    ],
    "conditions": [
      { "id": "paralyzed", "name": "Paralyzed",
        "restrictions": ["no_action", "no_bonus_action", "no_reaction", "no_move"] }
    ]
  }
})json";

GameSession combatDemo() {
  GameSession session;
  REQUIRE(session.loadRulesetFromJson(std::string(kMini)));
  return session;
}

} // namespace

TEST_CASE("CombatSession: initiative orders combatants by stat + die") {
  GameSession session = combatDemo();
  CombatSession combat(session.engine());
  const rpg_os::EntityHandle a = session.createCharacterHandle("geron");
  const rpg_os::EntityHandle b = session.createCharacterHandle("geron");
  const EntityId aId = a.entityId();
  const EntityId bId = b.entityId();
  (void)combat.addCombatant(a, 0);
  (void)combat.addCombatant(b, 1);

  // Both have Initiative 13; the dice decide: a rolls 6 (19), b rolls 1 (14).
  auto rng = script({6, 1});
  combat.beginRound(rng);
  REQUIRE(combat.next());
  CHECK(combat.current().entityId() == aId);
  CHECK(combat.round() == 1);
  REQUIRE(combat.next());
  CHECK(combat.current().entityId() == bId);
  CHECK_FALSE(combat.next()); // round over
  CHECK(combat.ended());
  CHECK(combat.combatantCount() == 2);
}

TEST_CASE("CombatSession: action budget is enforced per turn") {
  GameSession session = combatDemo();
  CombatSession combat(session.engine());
  const rpg_os::EntityHandle hero = session.createCharacterHandle("geron");
  const EntityId id = hero.entityId();
  (void)combat.addCombatant(hero, 0);

  auto rng = script({1}); // one initiative roll
  combat.beginRound(rng);
  REQUIRE(combat.next());
  combat.runCurrentTurn(rng);

  CHECK(combat.spendAction(id, ActionKind::Action));
  CHECK_FALSE(combat.spendAction(id, ActionKind::Action)); // only one standard action
  CHECK(combat.spendAction(id, ActionKind::BonusAction));
  CHECK_FALSE(combat.spendAction(id, ActionKind::BonusAction));
  CHECK(combat.remaining(id, ActionKind::Action) == 0);
  CHECK(combat.remaining(id, ActionKind::BonusAction) == 0);
  CHECK(combat.remaining(id, ActionKind::Reaction) == 1);
  CHECK(combat.canSpend(id, ActionKind::Action) == false);
}

TEST_CASE("CombatSession: durations decay across rounds via runCurrentTurn") {
  GameSession session = combatDemo();
  CombatSession combat(session.engine());
  const rpg_os::EntityHandle hero = session.createCharacterHandle("geron");
  const rpg_os::EntityHandle goblin = session.createCharacterHandle("geron");
  (void)combat.addCombatant(hero, 0);
  (void)combat.addCombatant(goblin, 1);

  // The hero is paralyzed for 2 rounds (2 ticks).
  session.applyCondition(*hero.resolve(), "paralyzed", 1, 2);
  CHECK(hero.resolve()->conditionStacks("paralyzed") == 1);

  // Round 1: both act; the hero's condition ticks 2 -> 1.
  auto rng = script({6, 1, 6, 1}); // initiative for round 1 and round 2
  combat.beginRound(rng);
  while (combat.next()) {
    combat.runCurrentTurn(rng);
  }
  CHECK(hero.resolve()->conditionStacks("paralyzed") == 1);

  // Round 2: the condition expires during the hero's turn.
  combat.beginRound(rng);
  while (combat.next()) {
    combat.runCurrentTurn(rng);
  }
  CHECK_FALSE(hero.resolve()->hasCondition("paralyzed"));
  CHECK(hero.resolve()->conditionStacks("paralyzed") == 0);
  CHECK(combat.round() == 2);
}

TEST_CASE("CombatSession: restrictions block spending actions") {
  GameSession session = combatDemo();
  CombatSession combat(session.engine());
  const rpg_os::EntityHandle hero = session.createCharacterHandle("geron");
  const EntityId id = hero.entityId();
  (void)combat.addCombatant(hero, 0);

  auto rng = script({1});
  combat.beginRound(rng);
  REQUIRE(combat.next());
  combat.runCurrentTurn(rng);

  // Unrestricted: can act normally.
  CHECK(combat.canSpend(id, ActionKind::Action));

  // Paralyzed: every action kind is refused before any budget is spent.
  session.applyCondition(*hero.resolve(), "paralyzed", 1);
  CHECK_FALSE(combat.canSpend(id, ActionKind::Action));
  CHECK_FALSE(combat.canSpend(id, ActionKind::BonusAction));
  CHECK_FALSE(combat.canSpend(id, ActionKind::Reaction));
  CHECK_FALSE(combat.canSpend(id, ActionKind::Move));
  CHECK_FALSE(combat.spendAction(id, ActionKind::Action));
  CHECK(combat.remaining(id, ActionKind::Action) == 1); // nothing spent
}
