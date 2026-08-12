// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file effects.hpp
 * @brief Active conditions with stack counts and durations.
 */
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// One active application of a condition: how many stacks, for how long.
struct ActiveEffect {
  std::string conditionId;
  int32_t stacks{1};
  int32_t remaining{0}; ///< remaining ticks; 0 = permanent until removed
  std::string source;   ///< what applied it (affliction / item / event id)
};

/// The active-effect timeline of a sheet: a list of @ref ActiveEffect with
/// stack-aware queries and a ticking step that decrements durations.
class EffectTimeline {
public:
  /// Adds `effect`, merging stacks into an existing entry with the same
  /// condition and source, and taking the longer remaining duration.
  void add(const ActiveEffect &effect) {
    for (ActiveEffect &entry : m_effects) {
      if (entry.conditionId == effect.conditionId && entry.source == effect.source) {
        entry.stacks += effect.stacks;
        if (effect.remaining > entry.remaining) {
          entry.remaining = effect.remaining;
        }
        return;
      }
    }
    m_effects.push_back(effect);
  }

  /// Removes every entry for `conditionId`; returns how many were removed.
  int32_t remove(std::string_view conditionId) {
    int32_t removed = 0;
    for (auto it = m_effects.begin(); it != m_effects.end();) {
      if (it->conditionId == conditionId) {
        it = m_effects.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  void clear() noexcept {
    m_effects.clear();
  }

  /// Total stacks of `conditionId` across all sources (0 when absent).
  [[nodiscard]] int32_t stacks(std::string_view conditionId) const {
    int32_t total = 0;
    for (const ActiveEffect &entry : m_effects) {
      if (entry.conditionId == conditionId) {
        total += entry.stacks;
      }
    }
    return total;
  }

  /// Whether at least one stack of `conditionId` is active.
  [[nodiscard]] bool has(std::string_view conditionId) const {
    return stacks(conditionId) > 0;
  }

  /// All active effects (immutable view).
  [[nodiscard]] const std::vector<ActiveEffect> &effects() const noexcept {
    return m_effects;
  }

  /// Decrements the remaining duration of every non-permanent effect and
  /// erases the expired ones; returns how many effects expired.
  int32_t tick() {
    int32_t expired = 0;
    for (auto it = m_effects.begin(); it != m_effects.end();) {
      if (it->remaining > 0) {
        --it->remaining;
        if (it->remaining == 0) {
          it = m_effects.erase(it);
          ++expired;
          continue;
        }
      }
      ++it;
    }
    return expired;
  }

  void toJson(Json &out) const {
    out = Json::array();
    for (const ActiveEffect &entry : m_effects) {
      out.push_back({{"condition", entry.conditionId},
                     {"stacks", entry.stacks},
                     {"remaining", entry.remaining},
                     {"source", entry.source}});
    }
  }

  void fromJson(const Json &in) {
    m_effects.clear();
    if (!in.is_array()) {
      return;
    }
    for (const Json &entry : in) {
      ActiveEffect effect;
      effect.conditionId = entry.value("condition", "");
      effect.stacks = entry.value("stacks", 1);
      effect.remaining = entry.value("remaining", 0);
      effect.source = entry.value("source", "");
      if (!effect.conditionId.empty()) {
        m_effects.push_back(std::move(effect));
      }
    }
  }

private:
  std::vector<ActiveEffect> m_effects;
};

} // namespace rpg_os
