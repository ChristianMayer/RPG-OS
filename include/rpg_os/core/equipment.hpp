// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file equipment.hpp
 * @ingroup rpg_os_core
 * @brief Worn gear: a slot -> item map.
 */
#pragma once

#include <map>
#include <rpg_os/common/json.hpp>
#include <string>
#include <string_view>

namespace rpg_os {

/// The worn/equipped gear of one sheet: one item per equipment slot.
///
/// @par Why slot -> itemId rather than a list of "equipped" flags?
/// The ruleset already declares the legal slots (`equipment_slots`); mapping
/// slot id to item id is the direct transcription of "what am I wearing
/// where". The engine validates that an item is actually allowed in a slot
/// before calling @ref equip; this type is the dumb storage underneath.
class Equipment {
public:
  /// Puts `itemId` into `slotId`, returning the item id that was previously
  /// there (empty when the slot was free). Overwriting is allowed here so the
  /// engine can decide the policy (swap vs. reject) at the call site.
  [[nodiscard]] std::string equip(std::string_view slotId, std::string_view itemId) {
    std::string previous;
    const auto it = m_slots.find(std::string(slotId));
    if (it != m_slots.end()) {
      previous = it->second;
    }
    m_slots[std::string(slotId)] = std::string(itemId);
    return previous;
  }

  /// Removes the item from `slotId`, returning its id (empty when the slot
  /// was free).
  [[nodiscard]] std::string unequip(std::string_view slotId) {
    const auto it = m_slots.find(std::string(slotId));
    if (it == m_slots.end()) {
      return {};
    }
    std::string itemId = it->second;
    m_slots.erase(it);
    return itemId;
  }

  /// Whether `slotId` is currently filled.
  [[nodiscard]] bool isEquipped(std::string_view slotId) const {
    return m_slots.find(std::string(slotId)) != m_slots.end();
  }

  /// The item id in `slotId`, or empty when the slot is free.
  [[nodiscard]] std::string_view itemIn(std::string_view slotId) const {
    const auto it = m_slots.find(std::string(slotId));
    return it == m_slots.end() ? std::string_view{} : std::string_view(it->second);
  }

  /// The full slot -> item map (immutable view).
  [[nodiscard]] const std::map<std::string, std::string> &slots() const noexcept {
    return m_slots;
  }

  void clear() noexcept {
    m_slots.clear();
  }

  void toJson(Json &out) const {
    out = Json::object();
    for (const auto &[slot, item] : m_slots) {
      out[slot] = item;
    }
  }

  void fromJson(const Json &in) {
    m_slots.clear();
    if (!in.is_object()) {
      return;
    }
    for (const auto &[slot, item] : in.items()) {
      m_slots[slot] = item.get<std::string>();
    }
  }

private:
  std::map<std::string, std::string> m_slots;
};

} // namespace rpg_os
