// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared cost / progression tables (rpg_os::CostTable).
#include <doctest/doctest.h>
#include <rpg_os/core/cost_table.hpp>

using rpg_os::CostTable;

namespace {

// D&D 5e XP -> level thresholds, stored as {xp threshold, level} pairs so the
// lookup key is the accumulated XP and the returned value is the level.
CostTable makeXpLevels() {
  return CostTable::fromEntries(CostTable::Kind::Threshold, 1.0,
                                {{0, 1}, {300, 2}, {900, 3}, {2700, 4}});
}

// DSA column A: AP cost for the Nth point of an ability.
CostTable makeDsaColumnA() {
  return CostTable::fromEntries(CostTable::Kind::Multiplier, 1.0,
                                {{1, 1}, {2, 2}, {3, 4}, {4, 7}, {5, 11}, {6, 16}, {7, 22}});
}

} // namespace

TEST_CASE("CostTable: threshold lookup maps XP to level") {
  const CostTable table = makeXpLevels();
  CHECK(table.kind() == CostTable::Kind::Threshold);
  CHECK(table.size() == 4);
  CHECK(table.lookup(0) == 1);
  CHECK(table.lookup(299) == 1);
  CHECK(table.lookup(300) == 2);
  CHECK(table.lookup(900) == 3);
  CHECK(table.lookup(2700) == 4);
  CHECK(table.lookup(999999) == 4);
}

TEST_CASE("CostTable: threshold lookup clamps below the lowest key") {
  const CostTable table = makeXpLevels();
  CHECK(table.lookup(-50) == 1);
}

TEST_CASE("CostTable: multiplier lookup returns per-index cost") {
  const CostTable table = makeDsaColumnA();
  CHECK(table.kind() == CostTable::Kind::Multiplier);
  CHECK(table.lookup(1) == 1);
  CHECK(table.lookup(3) == 4);
  CHECK(table.lookup(7) == 22);
  // Above the last key clamps to the last value.
  CHECK(table.lookup(8) == 22);
}

TEST_CASE("CostTable: empty table yields 0") {
  const CostTable table = CostTable::fromEntries(CostTable::Kind::Threshold, 1.0, {});
  CHECK(table.size() == 0);
  CHECK(table.lookup(10) == 0);
}

TEST_CASE("CostTable: fromEntries sorts ascending by key") {
  const CostTable table =
      CostTable::fromEntries(CostTable::Kind::Multiplier, 1.0, {{4, 7}, {1, 1}, {3, 4}, {2, 2}});
  const auto &entries = table.entries();
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].key == 1);
  CHECK(entries[1].key == 2);
  CHECK(entries[2].key == 3);
  CHECK(entries[3].key == 4);
  CHECK(table.lookup(3) == 4);
}

TEST_CASE("CostTable: base factor is preserved") {
  const CostTable table =
      CostTable::fromEntries(CostTable::Kind::Multiplier, 2.0, {{1, 10}, {2, 20}});
  CHECK(table.baseFactor() == doctest::Approx(2.0));
}
