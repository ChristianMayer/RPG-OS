// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file spellbook.hpp
 * @brief Known and prepared spells, plus vancian slot usage.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <rpg_os/common/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace rpg_os {

/// The spells a sheet knows and (where the ruleset uses preparation) has
/// prepared. Pool-based systems (TDE, BRP) only fill the known list; vancian
/// systems (D&D) additionally track a prepared subset and per-day slot usage.
class Spellbook {
public:
  /// Adds `spellId` to the known list (no-op when already known).
  void learn(std::string_view spellId) {
    if (!knows(spellId)) {
      m_known.push_back(std::string(spellId));
    }
  }

  /// Removes `spellId` from known (and prepared) lists; false when unknown.
  bool forget(std::string_view spellId) {
    if (!knows(spellId)) {
      return false;
    }
    m_known.erase(std::remove_if(m_known.begin(), m_known.end(),
                                 [&](const std::string &s) { return s == spellId; }),
                  m_known.end());
    unprepare(spellId);
    return true;
  }

  /// Marks `spellId` as prepared (requires knowing it); false otherwise.
  bool prepare(std::string_view spellId) {
    if (!knows(spellId)) {
      return false;
    }
    if (!hasPrepared(spellId)) {
      m_prepared.push_back(std::string(spellId));
    }
    return true;
  }

  /// Unmarks `spellId` as prepared.
  void unprepare(std::string_view spellId) {
    m_prepared.erase(std::remove_if(m_prepared.begin(), m_prepared.end(),
                                    [&](const std::string &s) { return s == spellId; }),
                     m_prepared.end());
  }

  /// Whether the spell is known.
  [[nodiscard]] bool knows(std::string_view spellId) const {
    for (const std::string &s : m_known) {
      if (s == spellId) {
        return true;
      }
    }
    return false;
  }

  /// Whether the spell is currently prepared.
  [[nodiscard]] bool hasPrepared(std::string_view spellId) const {
    for (const std::string &s : m_prepared) {
      if (s == spellId) {
        return true;
      }
    }
    return false;
  }

  /// All known spell ids (immutable view).
  [[nodiscard]] const std::vector<std::string> &known() const noexcept {
    return m_known;
  }
  /// Currently prepared spell ids (immutable view).
  [[nodiscard]] const std::vector<std::string> &prepared() const noexcept {
    return m_prepared;
  }

  /// Marks one spell slot of `level` as used (vancian casting).
  void markSlotUsed(int32_t level) {
    ++m_slotsUsed[level];
  }
  /// How many slots of `level` have been used today.
  [[nodiscard]] int32_t slotsUsed(int32_t level) const {
    const auto it = m_slotsUsed.find(level);
    return it == m_slotsUsed.end() ? 0 : it->second;
  }
  /// Restores all used slots (a long rest).
  void recoverAllSlots() {
    m_slotsUsed.clear();
  }

  void toJson(Json &out) const {
    out = Json::object();
    out["known"] = m_known;
    out["prepared"] = m_prepared;
    Json used = Json::object();
    for (const auto &[level, count] : m_slotsUsed) {
      used[std::to_string(level)] = count;
    }
    out["slots_used"] = used;
  }

  void fromJson(const Json &in) {
    m_known.clear();
    m_prepared.clear();
    m_slotsUsed.clear();
    if (!in.is_object()) {
      return;
    }
    if (in.contains("known") && in.at("known").is_array()) {
      for (const Json &id : in.at("known")) {
        m_known.push_back(id.get<std::string>());
      }
    }
    if (in.contains("prepared") && in.at("prepared").is_array()) {
      for (const Json &id : in.at("prepared")) {
        m_prepared.push_back(id.get<std::string>());
      }
    }
    if (in.contains("slots_used") && in.at("slots_used").is_object()) {
      for (const auto &[level, count] : in.at("slots_used").items()) {
        m_slotsUsed[std::stoi(level)] = count.get<int32_t>();
      }
    }
  }

private:
  std::vector<std::string> m_known;
  std::vector<std::string> m_prepared;
  std::map<int32_t, int32_t> m_slotsUsed; ///< level -> slots spent today
};

} // namespace rpg_os
