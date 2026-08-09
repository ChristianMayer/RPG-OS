// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Cost / progression tables.
//
// Rulesets define two kinds of tables (see the design spec, section 3.2):
//   - "threshold": a level is looked up from an accumulated value, e.g. D&D 5e
//     XP -> level (find the highest level whose XP threshold is <= the total).
//   - "multiplier": a per-index cost list, e.g. DSA column A (cost of the Nth
//     point), optionally scaled by a base factor.
// Both are served by a single floor-lookup over sorted (key, value) entries,
// so the universal loader and the generated specific-mode code share one type.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace rpg_os {

/// A progression/cost table.
class CostTable {
public:
  /// Table flavour. Lookup semantics are identical for both.
  enum class Kind {
    Threshold, ///< value for the largest key <= query
    Multiplier ///< cost indexed by integer key
  };

  /// One (key, value) pair. `key` is the lookup input (a threshold such as an
  /// XP total, or a per-point index for DSA-style tables) and `value` is the
  /// result (e.g. the level for that XP, or the cost for that index). The
  /// JSON loader is responsible for mapping ruleset fields onto this.
  struct Entry {
    int32_t key{0};
    int32_t value{0};
  };

  /// Builds a table, sorting the entries ascending by key. Duplicate keys keep
  /// their relative order; the last occurrence wins on lookup.
  static CostTable fromEntries(Kind kind, double baseFactor, std::vector<Entry> entries);

  /// The table flavour.
  [[nodiscard]] Kind kind() const noexcept {
    return m_kind;
  }

  /// Scaling factor applied to multiplier lookups (1.0 = none).
  [[nodiscard]] double baseFactor() const noexcept {
    return m_baseFactor;
  }

  /// The entries, sorted ascending by key.
  [[nodiscard]] const std::vector<Entry> &entries() const noexcept {
    return m_entries;
  }

  /// Number of entries.
  [[nodiscard]] std::size_t size() const noexcept {
    return m_entries.size();
  }

  /// Returns the value of the entry with the largest key <= `query` (floor
  /// lookup). Queries below the smallest key clamp to the smallest entry;
  /// an empty table yields 0.
  [[nodiscard]] int32_t lookup(int32_t query) const noexcept;

private:
  Kind m_kind{Kind::Threshold};
  double m_baseFactor{1.0};
  std::vector<Entry> m_entries;
};

inline CostTable CostTable::fromEntries(Kind kind, double baseFactor, std::vector<Entry> entries) {
  std::sort(entries.begin(), entries.end(),
            [](const Entry &a, const Entry &b) { return a.key < b.key; });
  CostTable table;
  table.m_kind = kind;
  table.m_baseFactor = baseFactor;
  table.m_entries = std::move(entries);
  return table;
}

inline int32_t CostTable::lookup(int32_t query) const noexcept {
  if (m_entries.empty()) {
    return 0;
  }
  // First entry with key > query.
  const auto it = std::upper_bound(m_entries.begin(), m_entries.end(), query,
                                   [](int32_t q, const Entry &entry) { return q < entry.key; });
  if (it == m_entries.begin()) {
    return m_entries.front().value;
  }
  if (it == m_entries.end()) {
    return m_entries.back().value;
  }
  return (it - 1)->value;
}

} // namespace rpg_os
