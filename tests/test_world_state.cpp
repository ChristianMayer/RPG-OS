// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_world_state.cpp
 * @brief Unit tests for the shared world bookkeeping (@c rpg_os::WorldState).
 *
 * Covers the party treasury, the day counter, and the JSON save-state shape,
 * including that absent state is left untouched on restore.
 */
#include <doctest/doctest.h>
#include <rpg_os/universal/world_state.hpp>

using rpg_os::Money;
using rpg_os::WorldState;

TEST_CASE("world_state: treasury and day counter bookkeeping") {
  WorldState state;
  CHECK(state.treasury.baseUnits() == 0);
  CHECK(state.day == 0);

  state.addToTreasury(Money{100});
  state.addToTreasury(Money{25});
  CHECK(state.treasury.baseUnits() == 125);

  state.removeFromTreasury(Money{40});
  CHECK(state.treasury.baseUnits() == 85);

  state.advanceDays(3);
  CHECK(state.day == 3);
}

TEST_CASE("world_state: save/load round-trips through the JSON shape") {
  WorldState state;
  state.addToTreasury(Money{85});
  state.advanceDays(3);

  rpg_os::Json out;
  state.toJson(out);
  CHECK(out["treasury"].get<int64_t>() == 85);
  CHECK(out["day"].get<int>() == 3);

  WorldState restored;
  restored.fromJson(out);
  CHECK(restored.treasury.baseUnits() == 85);
  CHECK(restored.day == 3);
}

TEST_CASE("world_state: fromJson leaves absent state unchanged") {
  WorldState state;
  state.treasury = Money{50};
  state.day = 7;
  state.fromJson(rpg_os::Json::object());
  CHECK(state.treasury.baseUnits() == 50);
  CHECK(state.day == 7);
}
