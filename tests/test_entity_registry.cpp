// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_entity_registry.cpp
 * @brief Tests for entity instance ids, handles, and the registry.
 *
 * The code review's gap was "no entity handles (EntityId, WeakRef), no target
 * selection references": characters were only addressable by a *type* id
 * shared by every instance of an archetype, and a @c GameSession held an
 * un-keyed bag of sheets. These tests pin the new per-instance identity:
 * every @c DynamicEntity carries a unique @c rpg_os::EntityId, a
 * @c GameSession can look entities up by id and hand out non-owning
 * @c EntityHandle s that expire when the entity is destroyed, and the id
 * survives a save/load round-trip so a restored session keeps addressing the
 * same living characters.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/core/entity.hpp>
#include <rpg_os/universal/game_session.hpp>
#include <string>

using rpg_os::DynamicEntity;
using rpg_os::EntityHandle;
using rpg_os::EntityId;
using rpg_os::GameSession;
using rpg_os::Json;

namespace {

/// Loads the TDE ruleset into a session; fails the test on load error.
GameSession tdeSession() {
  GameSession session;
  REQUIRE(
      session.loadRulesetFromFile(std::string(RPG_OS_SOURCE_DIR) + "/rulesets/tde5e_core.json"));
  return session;
}

} // namespace

TEST_CASE("EntityId: freshly minted ids are valid and unique") {
  const EntityId a = rpg_os::nextEntityId();
  const EntityId b = rpg_os::nextEntityId();
  CHECK(a.valid());
  CHECK(b.valid());
  CHECK(a != b);
  CHECK(EntityId{}.valid() == false);
  CHECK(a.toUint64() != 0);
}

TEST_CASE("EntityId: distinct instances of one archetype get distinct ids") {
  GameSession session = tdeSession();
  DynamicEntity &first = session.createCharacter("geron");
  DynamicEntity &second = session.createCharacter("geron");
  // Same archetype => same *type* id, different *instance* ids.
  CHECK(first.id() == second.id());
  CHECK(first.entityId().valid());
  CHECK(second.entityId().valid());
  CHECK(first.entityId() != second.entityId());
}

TEST_CASE("EntityRegistry: add/get/remove round-trips a live entity") {
  GameSession session = tdeSession();
  DynamicEntity &sheet = session.createCharacter("geron");
  const EntityId id = sheet.entityId();
  REQUIRE(id.valid());
  CHECK(session.entityCount() == 1);
  CHECK(session.findEntity(id) == &sheet);
  CHECK(session.entity(id).entityId() == id);
  CHECK(session.entity(id).resolve() == &sheet);
  CHECK(session.entities().size() == 1);
  CHECK(session.entities()[0] == &sheet);

  CHECK(session.removeEntity(id));
  CHECK(session.entityCount() == 0);
  CHECK(session.findEntity(id) == nullptr);
  CHECK(session.entity(id).expired());
  CHECK(session.entity(id).resolve() == nullptr);
  CHECK(session.removeEntity(id) == false);
}

TEST_CASE("EntityHandle: survives registry lookup and expires on removal") {
  GameSession session = tdeSession();
  const EntityHandle handle = session.createCharacterHandle("geron");
  REQUIRE(handle);
  REQUIRE(handle.resolve() != nullptr);
  const EntityId id = handle.entityId();
  CHECK(session.findEntity(id) == handle.resolve());
  CHECK(session.removeEntity(id));
  CHECK(handle.expired());
  CHECK_FALSE(handle);
  CHECK(handle.resolve() == nullptr);
}

TEST_CASE("EntityRegistry: entity id survives a save/load round-trip") {
  GameSession session = tdeSession();
  DynamicEntity &sheet = session.createCharacter("geron");
  const EntityId id = sheet.entityId();
  Json json;
  sheet.toJson(json);
  CHECK(json["entity_id"] == id.toUint64());

  DynamicEntity &restored = session.createCharacter("geron");
  restored.fromJson(json);
  CHECK(restored.entityId() == id);
}
