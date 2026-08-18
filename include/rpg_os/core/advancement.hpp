// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file advancement.hpp
 * @ingroup rpg_os_core
 * @brief Experience and advancement state.
 */
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>

namespace rpg_os {

/// The result of gaining experience: whether the character leveled up.
struct LevelUp {
  int32_t fromLevel{1}; ///< level before the gain
  int32_t toLevel{1};   ///< level after the gain
  bool leveled{false};  ///< true when the level changed
};

/// Experience / advancement bookkeeping for a sheet. XP is a running total;
/// `level` is derived from it via the ruleset's `xp_to_level` cost table by
/// the engine (the table's keys are XP thresholds, its values are levels).
/// `ap` is The Dark Eye's spendable Adventure Points. Which fields a ruleset
/// uses depends on its configuration; unused fields stay at their defaults.
class Advancement {
public:
  /// Adds `xp` to the running total.
  void gainXp(int64_t xp) {
    m_xp += xp;
  }
  /// The total experience accrued.
  [[nodiscard]] int64_t xp() const noexcept {
    return m_xp;
  }
  /// Current level (kept in sync with XP by the engine).
  int32_t level{1};
  /// Spendable advancement points (TDE AP).
  int32_t ap{0};

  void toJson(Json &out) const {
    out = Json::object();
    out["xp"] = m_xp;
    out["level"] = level;
    out["ap"] = ap;
  }

  void fromJson(const Json &in) {
    if (!in.is_object()) {
      return;
    }
    m_xp = in.value("xp", int64_t{0});
    level = in.value("level", 1);
    ap = in.value("ap", 0);
  }

private:
  int64_t m_xp{0};
};

} // namespace rpg_os
