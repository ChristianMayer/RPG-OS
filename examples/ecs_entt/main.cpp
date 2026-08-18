// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file main.cpp
 * @brief EnTT integration example (specific implementation of the ECS pattern).
 *
 * The dependency-free example (@c examples/ecs_registry) explains the pattern
 * with plain id-keyed maps; this one shows the same idea with the popular
 * EnTT ECS as the storage layer. A sheet's rules live in the
 * @c rpg_os::DynamicEntity (a component here), presentation and gameplay
 * data live in ordinary EnTT components, and systems are EnTT views. The
 * engine's observer events bridge the two: an @c OnResourceChanged event
 * updates the @c HpBar component in the registry without polling.
 *
 * EnTT (v3.14.0, MIT) is vendored as a single header under
 * @c include/rpg_os/third_party/entt/ — like the other third-party headers it
 * is never edited or reformatted.
 *
 * Usage: @c rpg_os_example_ecs_entt [project-root]
 */
#include <entt/entt.hpp>
#include <iostream>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <unordered_map>

namespace {

/// Position on a grid.
struct Position {
  int32_t x{0};
  int32_t y{0};
};

/// A display label.
struct Tag {
  std::string label;
};

/// What the UI shows for this entity's hit points (kept in sync by events).
struct HpBar {
  int32_t displayed{0};
};

/// The RPG OS sheet behind an EnTT entity — the component that carries the
/// actual rules state (stats, pools, conditions, effects).
struct Sheet {
  std::shared_ptr<rpg_os::DynamicEntity> entity;
};

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  rpg_os::RulesetEngine engine;
  if (!engine.loadRulesetFromFile(root + "/rulesets/tde5e_core.json")) {
    std::cerr << "failed to load tde5e_core.json: " << engine.lastError() << '\n';
    return 1;
  }

  entt::registry registry;

  // EnTT -> RPG OS: an EnTT entity carries a Sheet component that owns the
  // live DynamicEntity; the id map links RPG OS ids back to EnTT entities so
  // events can update components.
  std::unordered_map<rpg_os::EntityId, entt::entity> idToEntt;

  const auto addUnit = [&](const std::string &kind, const std::string &label, int32_t x,
                           int32_t y) {
    const entt::entity enttEntity = registry.create();
    registry.emplace<Tag>(enttEntity, label);
    registry.emplace<Position>(enttEntity, x, y);

    std::shared_ptr<rpg_os::DynamicEntity> entity =
        kind == "character" ? engine.createEntity("geron") : engine.createCreature("gotongi");
    registry.emplace<Sheet>(enttEntity, Sheet{entity});
    registry.emplace<HpBar>(enttEntity, entity->resource("LP"));

    const rpg_os::EntityId id = entity->entityId();
    idToEntt[id] = enttEntity;
    return id;
  };

  const rpg_os::EntityId heroId = addUnit("character", "hero", 0, 0);
  const rpg_os::EntityId enemyId = addUnit("creature", "gotongi", 5, 0);

  // RPG OS -> EnTT: an observer event updates the HpBar component of exactly
  // the EnTT entity whose sheet changed — the "UI health bar" use case from
  // the code review, without polling.
  (void)engine.registerEventListener(
      rpg_os::EventType::OnResourceChanged, [&](const rpg_os::EventData &data) {
        if (data.getString("resource", "") != "LP") {
          return;
        }
        const auto id = rpg_os::EntityId{static_cast<uint64_t>(data.getInt("instance_id"))};
        const auto it = idToEntt.find(id);
        if (it == idToEntt.end()) {
          return;
        }
        registry.get<HpBar>(it->second).displayed = data.getInt("new_value");
      });

  // A system (EnTT view) prints every unit.
  std::cout << "=== ECS pattern (EnTT) ===\n";
  auto printUnits = [&]() {
    registry.view<const Position, const Tag, const HpBar, const Sheet>().each(
        [](const Position &pos, const Tag &tag, const HpBar &bar, const Sheet &) {
          std::cout << "  " << tag.label << " @" << pos.x << "," << pos.y << "  HP "
                    << bar.displayed << '\n';
        });
  };
  printUnits();

  // A "system" resolves a rule through the engine: the hero is poisoned for 2
  // rounds and the enemy takes a hit. Both flow through the event bus, so the
  // HpBar components update themselves.
  auto sheetOf = [&](rpg_os::EntityId id) -> rpg_os::DynamicEntity & {
    return *registry.get<Sheet>(idToEntt[id]).entity;
  };
  engine.applyCondition(sheetOf(heroId), "pain", 1, 2);
  rpg_os::DynamicEntity attacker(engine.ruleset(), "attacker");
  (void)engine.applyDamage(attacker, sheetOf(enemyId), "LP", 8);

  std::cout << "After the gotongi takes an 8-point hit:\n";
  printUnits();
  std::cout << "hero pain stacks: " << sheetOf(heroId).conditionStacks("pain") << "\n";

  // Destroying an EnTT entity removes its Sheet (shared_ptr releases the
  // sheet); the hero's sheet is still alive through its own component.
  registry.destroy(idToEntt[enemyId]);
  std::cout << "gotongi removed from the ECS registry; hero sheet still alive: "
            << (sheetOf(heroId).resource("LP") > 0 ? "yes" : "no") << '\n';
  return 0;
}
