// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file event_system.hpp
 * @ingroup rpg_os_core
 * @brief Shared event system for reactive rules.
 *
 * Many tabletop rules are reactive: "when damage is taken, make a wound
 * check", "at the start of your turn, tick condition durations". The event
 * system turns those English phrases into a small observer pattern shared by
 * both modes:
 *   - the universal engine registers its JSON-driven rule triggers
 *     (@c EventTriggerDef, see @c ruleset_loader.hpp) against the bus and
 *     fires them from the damage / turn pipeline;
 *   - specific-mode and application code register plain callbacks for the
 *     same @c EventType values.
 *
 * @par Why this design?
 * Keeping the event types and bus in @c rpg_os::common (rather than inside
 * the universal mode) is what lets both modes react to the same events
 * without either depending on the other. The payload is a free-form @c Json
 * bag on purpose: rule effects are JSON-driven and need exactly the fields
 * their ruleset declares, so a fixed struct would either be too narrow (rules
 * cannot read custom fields) or too wide (everyone pays for fields they do
 * not use). The typed getters on @c EventData keep the common cases
 * ergonomic while the raw @c EventData::payload stays available for
 * rules that need something bespoke.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <rpg_os/common/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpg_os {

/**
 * Event types emitted by the engine.
 *
 * @par Why this closed set?
 * The set mirrors the moments the shipped rulesets actually react to — check
 * roll resolution, damage flow, turn cadence, and the bookkeeping layer's
 * state changes (inventory, currency, equipment, conditions, rests,
 * advancement, afflictions, time). Keeping it closed makes the two halves of
 * the engine — the JSON trigger runner and the user-facing callback API —
 * agree on a fixed vocabulary; an open-ended string-based event system would
 * let a typo in a ruleset silently produce an event nobody listens to.
 */
enum class EventType {
  /// Before a check's dice are rolled (may alter the roll / advantage).
  OnBeforeCheckRoll,
  /// After a check's dice are rolled (re-rolls, reroll failures).
  OnAfterCheckRoll,
  /// After raw damage has been rolled (resistances, thresholds, armor).
  OnDamageCalculated,
  /// After damage has been applied (concentration / wound checks).
  OnDamageTaken,
  /// Start of a combatant's turn (tick damage, condition durations).
  OnTurnStart,
  /// End of a combatant's turn.
  OnTurnEnd,
  /// An item was added to a sheet's inventory.
  OnItemAdded,
  /// An item was removed from a sheet's inventory.
  OnItemRemoved,
  /// Money changed hands (paid, received, spent, or converted).
  OnCurrencyChanged,
  /// An item was equipped or unequipped (slot changed).
  OnEquipChanged,
  /// A condition was added, removed, or expired.
  OnConditionChanged,
  /// A spell was cast.
  OnSpellCast,
  /// A sheet rested (short or long rest).
  OnRest,
  /// A sheet gained a level.
  OnLevelUp,
  /// An affliction (poison / disease / curse) was applied.
  OnAfflictionApplied,
  /// A base attribute or effective stat changed (a sheet's raw value was set,
  /// a temporary stat bonus was applied or expired).
  OnStatChanged,
  /// A check / save / attack was resolved (carries success and quality, so UI
  /// can hook "the hero failed the Climb check" without polling the sheet).
  OnCheckResolved,
  /// A resource pool changed (damage, healing, a spell's cost, a rest).
  OnResourceChanged,
  /// Temporary hit points changed.
  OnTempHpChanged,
  /// Game time advanced (calendar / duration ticking).
  OnTimePassed,
};

/**
 * Free-form payload carried by an event dispatch.
 *
 * @par Why free-form JSON instead of fixed fields?
 * A ruleset may declare any fields it needs in its event triggers, so the
 * payload cannot be a closed struct. Values are read through the typed
 * getters, which fall back to a caller-supplied default when a key is absent
 * or has the wrong type — this keeps rule code defensive without forcing
 * every listener to check key presence by hand.
 */
struct EventData {
  /// The payload bag (e.g. `{"damage": 12, "attacker_visible": true}`).
  /// Public so rules that need custom fields can read and mutate it directly;
  /// the getters are conveniences over this bag, not a separate storage.
  Json payload{};

  /// Reads an integer field, or `fallback` when absent / not an integer.
  [[nodiscard]] int32_t getInt(std::string_view key, int32_t fallback = 0) const {
    return payload.value(std::string(key), fallback);
  }

  /// Reads a boolean field, or `fallback` when absent / not a boolean.
  [[nodiscard]] bool getBool(std::string_view key, bool fallback = false) const {
    return payload.value(std::string(key), fallback);
  }

  /// Reads a string field, or `fallback` when absent / not a string.
  [[nodiscard]] std::string getString(std::string_view key, std::string fallback = {}) const {
    return payload.value(std::string(key), std::move(fallback));
  }
};

/**
 * A simple observer: listeners register against an @c EventType and are
 * invoked in registration order on dispatch. Removal is by a stable handle.
 *
 * @par Why this shape?
 * It is intentionally the smallest thing that satisfies both consumers. The
 * universal engine needs to *interleave* its JSON-driven rule triggers with
 * user listeners (triggers first, so user callbacks observe the final,
 * mutated payload), and application code needs to add/remove callbacks
 * without coupling to the engine internals. Registration-order dispatch gives
 * predictable semantics for rules that stack (e.g. two resistances), and the
 * handle-based removal avoids the dangling-iterator pitfalls of removing by
 * callback identity. The implementation is header-only and allocation-light
 * so the bus can be embedded in @ref RulesetEngine without extra plumbing.
 */
class EventBus {
public:
  /// Listener callback signature.
  using Callback = std::function<void(const EventData &)>;

  /// Registers a listener for `type` and returns a stable handle for removal.
  /// The handle is monotonically increasing, so a listener can never be
  /// mistaken for a different one — even after others were removed.
  [[nodiscard]] uint64_t addListener(EventType type, Callback callback) {
    const uint64_t id = m_nextId++;
    m_listeners[type].push_back(Entry{id, std::move(callback)});
    return id;
  }

  /// Removes a previously registered listener; returns false if not found.
  /// Removal is linear by handle; the listener sets are small in practice
  /// (a handful per event type), so simplicity wins over a map-of-maps.
  bool removeListener(uint64_t listenerId) {
    for (auto &kv : m_listeners) {
      auto &entries = kv.second;
      for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->m_id == listenerId) {
          entries.erase(it);
          return true;
        }
      }
    }
    return false;
  }

  /// Invokes all listeners registered for `type` in registration order.
  /// Dispatch is @c const: firing an event must not be able to modify the
  /// bus itself, only the payload, so concurrent readers of the listener set
  /// stay well-defined.
  void dispatch(EventType type, const EventData &data) const {
    const auto it = m_listeners.find(type);
    if (it == m_listeners.end()) {
      return;
    }
    for (const Entry &entry : it->second) {
      entry.m_callback(data);
    }
  }

  /// Number of listeners for a type (diagnostics / tests).
  [[nodiscard]] std::size_t listenerCount(EventType type) const noexcept {
    const auto it = m_listeners.find(type);
    return it == m_listeners.end() ? 0 : it->second.size();
  }

private:
  struct Entry {
    uint64_t m_id;
    Callback m_callback;
  };
  std::unordered_map<EventType, std::vector<Entry>> m_listeners;
  uint64_t m_nextId{1};
};

} // namespace rpg_os
