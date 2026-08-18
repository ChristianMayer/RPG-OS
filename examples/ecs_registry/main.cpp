// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file main.cpp
 * @brief Dependency-free ECS pattern example.
 *
 * RPG OS deliberately does not bundle an entity-component-system: entities are
 * live @c rpg_os::DynamicEntity sheets, components are whatever the
 * application wants to store per entity, and systems are plain functions.
 * What the library now *does* provide are the pieces an ECS needs:
 *
 *   - @c rpg_os::EntityId / @c rpg_os::EntityHandle — a stable identity per
 *     living sheet, so component maps can be keyed by entity and a handle
 *     safely reports "this creature died" instead of dangling,
 *   - @c rpg_os::GameSession::entities() — iteration for systems, and
 *   - the observer events (@c OnResourceChanged, @c OnCheckResolved, ...) so
 *     presentation state (a health bar) updates on a state change instead of
 *     polling the sheet every frame.
 *
 * This example shows the pattern with zero third-party dependencies; the
 * @c examples/ecs_entt example is the same idea with EnTT as the storage
 * layer.
 *
 * Usage: @c rpg_os_example_ecs_registry [project-root]
 */
#include <iostream>
#include <rpg_os/universal/combat_session.hpp>
#include <rpg_os/universal/game_session.hpp>
#include <string>
#include <unordered_map>

namespace {

/// A component: where on a grid the entity stands.
struct Position {
  int32_t x{0};
  int32_t y{0};
};

/// A component: a display label for the entity.
struct Tag {
  std::string label;
};

/// A component: what the UI currently shows for this entity's hit points.
/// Kept separate from the rulesheet so "the number on screen" is the app's
/// data; the observer event below keeps it in sync without polling.
struct HpBar {
  int32_t displayed{0};
};

/// The "render" system: snapshot every sheet's current hit points into its
/// @ref HpBar component. In a real game this would be called once at startup
/// and then only after load; live changes arrive through events.
void renderHpSystem(const std::vector<rpg_os::DynamicEntity *> &sheets,
                    std::unordered_map<rpg_os::EntityId, HpBar> &bars) {
  for (const rpg_os::DynamicEntity *sheet : sheets) {
    bars[sheet->entityId()] = HpBar{sheet->resource("LP")};
  }
}

/// Prints the current component state for every entity (the demo's "screen").
void printEntities(const std::vector<rpg_os::DynamicEntity *> &sheets,
                   const std::unordered_map<rpg_os::EntityId, Position> &positions,
                   const std::unordered_map<rpg_os::EntityId, Tag> &tags,
                   const std::unordered_map<rpg_os::EntityId, HpBar> &bars) {
  for (const rpg_os::DynamicEntity *sheet : sheets) {
    const rpg_os::EntityId id = sheet->entityId();
    const Position &pos = positions.at(id);
    const Tag &tag = tags.at(id);
    const HpBar &bar = bars.at(id);
    std::cout << "  " << tag.label << " @" << pos.x << "," << pos.y << "  HP " << bar.displayed
              << "/" << sheet->resource("LP") << '\n';
  }
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  rpg_os::GameSession session;
  if (!session.loadRulesetFromFile(root + "/rulesets/tde5e_core.json")) {
    std::cerr << "failed to load tde5e_core.json: " << session.lastError() << '\n';
    return 1;
  }

  // 1) Entities: two living sheets, addressed by stable handles. The handles
  //    stay valid even after the entities are removed from the session.
  const rpg_os::EntityHandle hero = session.createCharacterHandle("geron");
  const rpg_os::EntityHandle enemy = session.createCreatureHandle("gotongi");
  const rpg_os::EntityId heroId = hero.entityId();
  const rpg_os::EntityId enemyId = enemy.entityId();

  // 2) Components: plain application data, keyed by entity id.
  std::unordered_map<rpg_os::EntityId, Position> positions;
  std::unordered_map<rpg_os::EntityId, Tag> tags;
  std::unordered_map<rpg_os::EntityId, HpBar> bars;
  positions[heroId] = {0, 0};
  positions[enemyId] = {5, 0};
  tags[heroId] = {"hero"};
  tags[enemyId] = {"gotongi"};
  renderHpSystem(session.entities(), bars);

  std::cout << "=== ECS pattern (dependency-free registry) ===\n";
  printEntities(session.entities(), positions, tags, bars);

  // 3) An observer event: when the hero's hit points change, update only that
  //    entity's HpBar component — no per-frame polling of the sheet.
  (void)session.onEvent(rpg_os::EventType::OnResourceChanged, [&](const rpg_os::EventData &data) {
    if (data.getString("resource", "") != "LP") {
      return;
    }
    const auto id = rpg_os::EntityId{static_cast<uint64_t>(data.getInt("instance_id"))};
    const auto it = bars.find(id);
    if (it != bars.end()) {
      it->second.displayed = data.getInt("new_value");
    }
  });

  // 4) A system applies a rule: the hero is poisoned for 2 rounds, and a
  //    CombatSession drives the turns so the duration decays and the expiry
  //    fires OnConditionChanged. The engine owns the bookkeeping; the
  //    application owns the presentation.
  session.applyCondition(*hero.resolve(), "pain", 1, 2);
  rpg_os::CombatSession combat(session.engine());
  (void)combat.addCombatant(hero, 0);
  (void)combat.addCombatant(enemy, 1);
  rpg_os::DefaultRandom rng;
  combat.beginRound(rng);
  while (combat.next()) {
    combat.runCurrentTurn(rng);
  }

  // The hero takes a hit (through the engine's damage pipeline), which flows
  // through OnResourceChanged and updates the HpBar component automatically.
  rpg_os::DynamicEntity attacker(session.engine().ruleset(), "attacker");
  (void)session.engine().applyDamage(attacker, *hero.resolve(), "LP", 6);
  std::cout << "After a 6-point hit and one round (pain: "
            << hero.resolve()->conditionStacks("pain") << " stack left):\n";
  printEntities(session.entities(), positions, tags, bars);

  // 5) The handle layer: removing the enemy makes its handle expire safely.
  std::cout << "Enemy alive: " << (enemy ? "yes" : "no") << " -> ";
  (void)session.removeEntity(enemyId);
  std::cout << (enemy ? "still yes" : "no, handle expired") << '\n';
  return 0;
}
