// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file world_state.hpp
 * @brief Minimal shared world bookkeeping.
 *
 * The library deliberately stays out of "higher-level" world concerns — plot,
 * world maps, and encounter design belong to the application. What it does
 * keep is the bookkeeping that several characters genuinely share: a party
 * treasury and a running day counter for duration ticking. Everything else
 * (factions, calendars with named months, ...) is left to the application.
 */
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/money.hpp>

namespace rpg_os {

/// Shared bookkeeping state for one campaign.
class WorldState {
public:
  /// The party's pooled wealth (independent of any character's purse).
  Money treasury{0};
  /// The in-game day counter (used for effect durations and rest cadence).
  int32_t day{0};

  /// Adds `amount` to the treasury.
  void addToTreasury(const Money &amount) {
    treasury += amount;
  }
  /// Removes `amount` from the treasury (may go negative = party debt).
  void removeFromTreasury(const Money &amount) {
    treasury -= amount;
  }
  /// Advances the day counter by `days`.
  void advanceDays(int32_t days) {
    day += days;
  }

  void toJson(Json &out) const {
    out = Json::object();
    out["treasury"] = treasury.baseUnits();
    out["day"] = day;
  }

  void fromJson(const Json &in) {
    if (!in.is_object()) {
      return;
    }
    if (in.contains("treasury") && in.at("treasury").is_number_integer()) {
      treasury = Money{in.at("treasury").get<int64_t>()};
    }
    day = in.value("day", day);
  }
};

} // namespace rpg_os
