// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file entity_registry.hpp
 * @ingroup rpg_os_universal
 * @brief Entity handles and a live-instance registry (universal mode).
 *
 * The code review's gap was "no entity handles (EntityId, WeakRef) or target
 * selection references": a @c DynamicEntity was only addressable by its
 * *archetype* @c id(), shared by every instance of that kind, and a
 * @c GameSession held an un-keyed bag of sheets. This header closes that gap
 * with two small pieces:
 *   - @c EntityHandle — a non-owning, stable reference (a @c std::weak_ptr
 *     wrapper) that stays meaningful across lookups and goes "expired" when
 *     the entity is destroyed, and
 *   - @c EntityRegistry — the owner of the live entities, indexed by the
 *     per-instance @c rpg_os::EntityId that every @c DynamicEntity carries.
 *
 * @par Why handles instead of raw pointers?
 * A raw pointer is a *reference* but not a *handle*: it does not say whether
 * the pointee is still alive, and it cannot be stored safely next to code
 * that removes entities. A handle that can report @c EntityHandle::expired
 * lets a @c CombatSession (or any long-lived targeting code) keep a list of
 * "these combatants" without risking a dangling dereference after one of them
 * dies. The id travels with the handle, so identity survives the entity's
 * lifetime even when the pointee does not.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <rpg_os/core/entity.hpp>
#include <rpg_os/universal/dynamic_entity.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpg_os {

/// A non-owning, stable reference to a live @c DynamicEntity.
///
/// A handle is created by an @ref EntityRegistry (or a @ref GameSession) and
/// can be copied freely. It never owns the entity, so it never has to delete
/// anything; it only records the entity's @ref EntityId and a @c std::weak_ptr
/// used to check liveness and to obtain a pointer when the entity is alive.
///
/// @par Why store the id *and* a weak pointer?
/// The id answers "which entity is this" and stays valid forever; the weak
/// pointer answers "is it still alive / give me access". Keeping both means
/// a dead entity can still be *identified* in a log or an event payload even
/// though it can no longer be dereferenced.
class EntityHandle {
public:
  /// Creates an empty (null) handle that refers to nothing.
  EntityHandle() = default;

  /// Creates a handle for `id` backed by a weak reference to the entity.
  EntityHandle(EntityId id, std::weak_ptr<DynamicEntity> weak)
      : m_id(id), m_weak(std::move(weak)) {}

  /// The live entity this handle refers to, or @c nullptr when it has been
  /// destroyed. The returned pointer is valid only while the owning registry
  /// still holds the entity.
  [[nodiscard]] DynamicEntity *resolve() const noexcept {
    return m_weak.lock().get();
  }

  /// Whether the referenced entity has been destroyed.
  [[nodiscard]] bool expired() const noexcept {
    return m_weak.expired();
  }

  /// Whether this handle refers to a live entity.
  [[nodiscard]] explicit operator bool() const noexcept {
    return !m_weak.expired();
  }

  /// The id this handle refers to (valid even after the entity is gone).
  [[nodiscard]] EntityId entityId() const noexcept {
    return m_id;
  }

  /// Handles are equal when they refer to the same entity id.
  [[nodiscard]] bool operator==(const EntityHandle &other) const noexcept {
    return m_id == other.m_id;
  }

private:
  EntityId m_id{};
  std::weak_ptr<DynamicEntity> m_weak;
};

/// Owns the live entities and looks them up by stable @ref EntityId.
///
/// This is the universal-mode entity pool: entities are created by the engine
/// as @c std::shared_ptr, handed to the registry, and thereafter addressed by
/// id (lookup), by handle (non-owning reference), or by iteration. It is the
/// piece that lets an application answer "give me that character", "is this
/// creature still alive", and "list everyone on the field" without keeping
/// raw pointers itself.
class EntityRegistry {
public:
  /// Registers `entity` under its own @c DynamicEntity::entityId and takes
  /// shared ownership; returns the id (which is unique per process).
  [[nodiscard]] EntityId add(std::shared_ptr<DynamicEntity> entity) {
    const EntityId id = entity->entityId();
    m_entities[id] = std::move(entity);
    return id;
  }

  /// Looks up a live entity by id; @c nullptr when absent.
  [[nodiscard]] DynamicEntity *get(EntityId id) const {
    const auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : it->second.get();
  }

  /// A non-owning handle to the entity with `id` (a null handle when absent).
  [[nodiscard]] EntityHandle handle(EntityId id) const {
    const auto it = m_entities.find(id);
    if (it == m_entities.end()) {
      return EntityHandle{};
    }
    return EntityHandle{id, it->second};
  }

  /// Whether an entity with `id` is registered.
  [[nodiscard]] bool contains(EntityId id) const {
    return m_entities.contains(id);
  }

  /// Number of live entities.
  [[nodiscard]] std::size_t size() const noexcept {
    return m_entities.size();
  }

  /// Removes `id` from the registry (destroying the last owner of the
  /// entity); returns false when the id was not present.
  bool remove(EntityId id) {
    return m_entities.erase(id) != 0;
  }

  /// Removes every entity (destroying the session's last owners). Used when a
  /// saved state is loaded over the current one.
  void clear() {
    m_entities.clear();
  }

  /// All live entities, in unspecified order.
  [[nodiscard]] std::vector<DynamicEntity *> all() const {
    std::vector<DynamicEntity *> result;
    result.reserve(m_entities.size());
    for (const auto &entry : m_entities) {
      result.push_back(entry.second.get());
    }
    return result;
  }

private:
  std::unordered_map<EntityId, std::shared_ptr<DynamicEntity>> m_entities;
};

} // namespace rpg_os
