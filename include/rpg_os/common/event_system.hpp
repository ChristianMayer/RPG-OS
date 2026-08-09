// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared event system.
//
// Both modes use the same EventType set and EventBus. The universal engine
// registers JSON-driven effects against the bus; specific-mode code registers
// plain callbacks. EventData carries a free-form JSON payload so rule code can
// read exactly the fields it needs.
#pragma once

#include <cstdint>
#include <functional>
#include <rpg_os/common/json.hpp>
#include <rpg_os/common/types.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpg_os {

/// Event types emitted by the engine (see the design spec, section 7.1).
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
};

/// Free-form payload carried by an event dispatch. Values are read via the
/// typed getters; unknown keys fall back to the supplied default.
struct EventData {
  /// The payload bag (e.g. `{"damage": 12, "attacker_visible": true}`).
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

/// A simple observer: listeners register against an EventType and are invoked
/// in registration order on dispatch. Removal is by the returned handle.
/// Header-only and shared by both modes.
class EventBus {
public:
  /// Listener callback signature.
  using Callback = std::function<void(const EventData &)>;

  /// Registers a listener for `type` and returns a stable handle for removal.
  [[nodiscard]] uint64_t addListener(EventType type, Callback callback) {
    const uint64_t id = m_nextId++;
    m_listeners[type].push_back(Entry{id, std::move(callback)});
    return id;
  }

  /// Removes a previously registered listener; returns false if not found.
  bool removeListener(uint64_t listenerId) {
    for (auto &kv : m_listeners) {
      auto &entries = kv.second;
      for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (it->id == listenerId) {
          entries.erase(it);
          return true;
        }
      }
    }
    return false;
  }

  /// Invokes all listeners registered for `type` in registration order.
  void dispatch(EventType type, const EventData &data) const {
    const auto it = m_listeners.find(type);
    if (it == m_listeners.end()) {
      return;
    }
    for (const Entry &entry : it->second) {
      entry.callback(data);
    }
  }

  /// Number of listeners for a type (diagnostics / tests).
  [[nodiscard]] std::size_t listenerCount(EventType type) const {
    const auto it = m_listeners.find(type);
    return it == m_listeners.end() ? 0 : it->second.size();
  }

private:
  struct Entry {
    uint64_t id;
    Callback callback;
  };
  std::unordered_map<EventType, std::vector<Entry>> m_listeners;
  uint64_t m_nextId{1};
};

} // namespace rpg_os
