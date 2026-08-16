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

/// A temporary modifier to one stat, applied for a duration of ticks (a buff
/// such as The Dark Eye's Perception-boosting spell, a D&D +AC spell, ...).
struct StatBonus {
  std::string stat;     ///< the stat id the bonus applies to
  int32_t value{0};     ///< the flat amount added to the effective stat
  int32_t remaining{0}; ///< remaining ticks; 0 = permanent until removed
  std::string source;   ///< what applied it (spell / item / event id)
};

/// A temporary die that is rolled and added to checks of a matching scope for
/// a duration of ticks (D&D's Bless +1d4 to attack rolls and saves, a bardic
/// inspiration die, ...).
struct BonusDie {
  std::string dice;     ///< the die expression, e.g. "1d4"
  std::string scope;    ///< which checks it applies to (see scope matching)
  int32_t remaining{0}; ///< remaining ticks; 0 = permanent until removed
  std::string source;   ///< what applied it (spell / item / event id)
};

/// A stored effect that re-resolves on its own turn phase — a recurring
/// (per-round) damage / heal / condition, or a monster's regeneration. The
/// record is the original effect JSON with the `ongoing` field stripped, so
/// re-resolving it through the engine applies exactly the base effect.
struct OngoingEffect {
  rpg_os::Json effect;  ///< the effect record to re-apply each phase
  std::string phase;    ///< "start_of_turn" | "end_of_turn"
  int32_t remaining{0}; ///< how many more times it re-applies
  std::string source;   ///< what applied it (spell / item / event id)
};

/// A record of a poison / disease / curse applied to a sheet — bookkeeping
/// for "this affliction is active, and these conditions its effects applied".
///
/// @par Why in the shared core?
/// @c DynamicEntity nests its own copy of this record; sharing the value type
/// here (and aliasing it there) lets the generated specific-mode characters
/// serialize the exact same save-state shape without depending on the
/// universal mode.
struct AppliedAffliction {
  std::string section;                 ///< "poisons" / "diseases" / "curses"
  std::string id;                      ///< affliction record id
  std::vector<std::string> conditions; ///< condition ids its effects applied
};

/// The active-effect timeline of a sheet: a list of @ref ActiveEffect with
/// stack-aware queries and a ticking step that decrements durations, plus the
/// sheet's temporary stat bonuses and recurring (ongoing) effects — all three
/// tick together and serialize together, so one timeline is the whole
/// "what is currently affecting this sheet" store.
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
    m_statBonuses.clear();
    m_bonusDice.clear();
    m_ongoing.clear();
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

  /// Adds a temporary stat bonus, merging into an existing bonus on the same
  /// stat from the same source (values add, the longer duration wins).
  void addBonus(const StatBonus &bonus) {
    for (StatBonus &entry : m_statBonuses) {
      if (entry.stat == bonus.stat && entry.source == bonus.source) {
        entry.value += bonus.value;
        if (bonus.remaining > entry.remaining) {
          entry.remaining = bonus.remaining;
        }
        return;
      }
    }
    m_statBonuses.push_back(bonus);
  }

  /// Removes every stat bonus on `stat`; returns how many were removed.
  int32_t removeBonuses(std::string_view stat) {
    int32_t removed = 0;
    for (auto it = m_statBonuses.begin(); it != m_statBonuses.end();) {
      if (it->stat == stat) {
        it = m_statBonuses.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  /// The total temporary bonus currently applying to `stat` (0 when none).
  [[nodiscard]] int32_t bonusFor(std::string_view stat) const {
    int32_t total = 0;
    for (const StatBonus &bonus : m_statBonuses) {
      if (bonus.stat == stat) {
        total += bonus.value;
      }
    }
    return total;
  }

  /// All temporary stat bonuses (immutable view).
  [[nodiscard]] const std::vector<StatBonus> &statBonuses() const noexcept {
    return m_statBonuses;
  }

  /// Adds a temporary bonus die (stacked; each entry rolls and adds).
  void addBonusDie(const BonusDie &bonus) {
    m_bonusDice.push_back(bonus);
  }

  /// Removes every bonus die; returns how many were removed.
  int32_t removeBonusDice() {
    const int32_t removed = static_cast<int32_t>(m_bonusDice.size());
    m_bonusDice.clear();
    return removed;
  }

  /// All temporary bonus dice (immutable view).
  [[nodiscard]] const std::vector<BonusDie> &bonusDice() const noexcept {
    return m_bonusDice;
  }

  /// Registers a recurring (ongoing) effect to re-apply on `phase`.
  void addOngoing(const OngoingEffect &effect) {
    m_ongoing.push_back(effect);
  }

  /// Removes every recurring effect for `source`; returns how many removed.
  int32_t removeOngoing(std::string_view source) {
    int32_t removed = 0;
    for (auto it = m_ongoing.begin(); it != m_ongoing.end();) {
      if (it->source == source) {
        it = m_ongoing.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
    return removed;
  }

  /// All recurring effects (immutable view).
  [[nodiscard]] const std::vector<OngoingEffect> &ongoing() const noexcept {
    return m_ongoing;
  }

  /// Decrements the remaining duration of every non-permanent condition, stat
  /// bonus, and bonus die; erases the expired ones; returns how many expired.
  /// Recurring effects are NOT aged here — they age by firing, via
  /// @ref tickPhase (driven by the engine's turn processing).
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
    for (auto it = m_statBonuses.begin(); it != m_statBonuses.end();) {
      if (it->remaining > 0) {
        --it->remaining;
        if (it->remaining == 0) {
          it = m_statBonuses.erase(it);
          ++expired;
          continue;
        }
      }
      ++it;
    }
    for (auto it = m_bonusDice.begin(); it != m_bonusDice.end();) {
      if (it->remaining > 0) {
        --it->remaining;
        if (it->remaining == 0) {
          it = m_bonusDice.erase(it);
          ++expired;
          continue;
        }
      }
      ++it;
    }
    return expired;
  }

  /// Decrements the remaining repetitions of every recurring effect on
  /// `phase` and erases the expired ones; returns how many expired. Called
  /// after the engine fires that phase's recurring effects.
  int32_t tickPhase(std::string_view phase) {
    int32_t expired = 0;
    for (auto it = m_ongoing.begin(); it != m_ongoing.end();) {
      if (it->phase == phase && it->remaining > 0) {
        --it->remaining;
        if (it->remaining == 0) {
          it = m_ongoing.erase(it);
          ++expired;
          continue;
        }
      }
      ++it;
    }
    return expired;
  }

  void toJson(Json &out) const {
    out = Json::object();
    Json conditions = Json::array();
    for (const ActiveEffect &entry : m_effects) {
      conditions.push_back({{"condition", entry.conditionId},
                            {"stacks", entry.stacks},
                            {"remaining", entry.remaining},
                            {"source", entry.source}});
    }
    out["conditions"] = conditions;
    Json bonuses = Json::array();
    for (const StatBonus &bonus : m_statBonuses) {
      bonuses.push_back({{"stat", bonus.stat},
                         {"value", bonus.value},
                         {"remaining", bonus.remaining},
                         {"source", bonus.source}});
    }
    out["stat_bonuses"] = bonuses;
    Json dice = Json::array();
    for (const BonusDie &entry : m_bonusDice) {
      dice.push_back({{"dice", entry.dice},
                      {"scope", entry.scope},
                      {"remaining", entry.remaining},
                      {"source", entry.source}});
    }
    out["bonus_dice"] = dice;
    Json ongoing = Json::array();
    for (const OngoingEffect &entry : m_ongoing) {
      Json record = {{"effect", entry.effect},
                     {"phase", entry.phase},
                     {"remaining", entry.remaining},
                     {"source", entry.source}};
      ongoing.push_back(record);
    }
    out["ongoing"] = ongoing;
  }

  void fromJson(const Json &in) {
    m_effects.clear();
    m_statBonuses.clear();
    m_bonusDice.clear();
    m_ongoing.clear();
    if (!in.is_object()) {
      return;
    }
    if (in.contains("conditions") && in.at("conditions").is_array()) {
      for (const Json &entry : in.at("conditions")) {
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
    if (in.contains("stat_bonuses") && in.at("stat_bonuses").is_array()) {
      for (const Json &entry : in.at("stat_bonuses")) {
        StatBonus bonus;
        bonus.stat = entry.value("stat", "");
        bonus.value = entry.value("value", 0);
        bonus.remaining = entry.value("remaining", 0);
        bonus.source = entry.value("source", "");
        if (!bonus.stat.empty()) {
          m_statBonuses.push_back(std::move(bonus));
        }
      }
    }
    if (in.contains("bonus_dice") && in.at("bonus_dice").is_array()) {
      for (const Json &entry : in.at("bonus_dice")) {
        BonusDie die;
        die.dice = entry.value("dice", "");
        die.scope = entry.value("scope", "all");
        die.remaining = entry.value("remaining", 0);
        die.source = entry.value("source", "");
        if (!die.dice.empty()) {
          m_bonusDice.push_back(std::move(die));
        }
      }
    }
    if (in.contains("ongoing") && in.at("ongoing").is_array()) {
      for (const Json &entry : in.at("ongoing")) {
        OngoingEffect ongoing;
        if (entry.contains("effect")) {
          ongoing.effect = entry.at("effect");
        }
        ongoing.phase = entry.value("phase", "end_of_turn");
        ongoing.remaining = entry.value("remaining", 0);
        ongoing.source = entry.value("source", "");
        if (ongoing.effect.is_object()) {
          m_ongoing.push_back(std::move(ongoing));
        }
      }
    }
  }

private:
  std::vector<ActiveEffect> m_effects;
  std::vector<StatBonus> m_statBonuses;
  std::vector<BonusDie> m_bonusDice;
  std::vector<OngoingEffect> m_ongoing;
};

} // namespace rpg_os
