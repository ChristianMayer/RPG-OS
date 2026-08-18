// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cost_table.hpp
 * @ingroup rpg_os_core
 * @brief Cost / progression tables.
 *
 * Rulesets define two superficially different kinds of table:
 *   - @b threshold: a result is looked up from an accumulated value, e.g. D&D
 *     5e XP -> level (find the highest level whose XP threshold is <= the
 *     total);
 *   - @b multiplier: a per-index cost list, e.g. a DSA improvement column
 *     (the cost of the Nth point), optionally scaled by a base factor.
 *
 * @par Why one type for both?
 * Both flavours reduce to the same operation — a floor lookup over sorted
 * (key, value) entries — differing only in *how the ruleset JSON names the
 * fields*. Collapsing them into a single @c CostTable lets the universal
 * loader and the generated specific-mode code share one implementation and
 * one set of tests, instead of two look-alike classes that would inevitably
 * drift apart. The JSON loader is the only place that knows which flavour a
 * ruleset declared; everything downstream just asks "what is the value for
 * this key?".
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace rpg_os {

/**
 * A progression/cost table.
 *
 * Instances are immutable after construction (entries are sorted once by
 * @ref fromEntries), which makes them safe to share between the engine and
 * generated code, and trivially const-correct in hot paths.
 */
class CostTable {
public:
  /// Table flavour. Lookup semantics are identical for both, so the flavour
  /// is metadata for diagnostics and codegen rather than a behavioural switch.
  enum class Kind {
    Threshold, ///< value for the largest key <= query
    Multiplier ///< cost indexed by integer key
  };

  /**
   * One (key, value) pair. `key` is the lookup input (a threshold such as an
   * XP total, or a per-point index for DSA-style tables) and `value` is the
   * result (e.g. the level for that XP, or the cost for that index). The JSON
   * loader is responsible for mapping ruleset fields onto this uniform shape.
   */
  struct Entry {
    int32_t key{0};
    int32_t value{0};
  };

  /// Builds a table, sorting the entries ascending by key. Duplicate keys keep
  /// their relative order; the last occurrence wins on lookup.
  ///
  /// @par Why sort at construction?
  /// Normalising to sorted order at build time means @ref lookup can use a
  /// binary search instead of scanning, and the universal loader and the
  /// generated code do not each have to remember to keep their entries sorted.
  static CostTable fromEntries(Kind kind, double baseFactor, std::vector<Entry> entries);

  /// The table flavour.
  [[nodiscard]] Kind kind() const noexcept {
    return m_kind;
  }

  /// Scaling factor applied to multiplier lookups (1.0 = none). Exposed so
  /// the code generator can emit the same factor into compiled tables.
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
  ///
  /// @par Why clamp at the extremes instead of failing?
  /// A character may legitimately ask for a level below the first threshold
  /// (level 1) or beyond the highest (a very high XP total). Silently clamping
  /// to the nearest defined entry matches how these tables are used in the
  /// source games and keeps the API total (no error path for out-of-range
  /// queries that are actually normal).
  [[nodiscard]] int32_t lookup(int32_t query) const noexcept;

private:
  Kind m_kind{Kind::Threshold};
  double m_baseFactor{1.0};
  std::vector<Entry> m_entries;
};

inline CostTable CostTable::fromEntries(Kind kind, double baseFactor, std::vector<Entry> entries) {
  // A stable sort is not needed: duplicate keys are resolved by "last wins",
  // which is unaffected by which of two equal-keyed entries comes first — the
  // later-in-vector one simply ends up later in the sorted order too.
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
  // std::upper_bound finds the first entry with key > query; the answer is
  // the entry just before it (or the first/last when the query falls outside
  // the table, handled by the clamp below).
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
