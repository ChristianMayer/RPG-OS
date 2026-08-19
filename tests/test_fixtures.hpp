// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_fixtures.hpp
 * @brief Shared fixtures for the bookkeeping test suite.
 *
 * The bookkeeping tests (test_money.cpp, test_inventory.cpp, ...) all start
 * from the same include set, using-declarations and the @c dndCoins test
 * coinage. Centralising them here means a new test only includes this one
 * header instead of copying the block.
 */
#pragma once

#include "test_util.hpp"

#include <rpg_os/core/advancement.hpp>
#include <rpg_os/core/effects.hpp>
#include <rpg_os/core/equipment.hpp>
#include <rpg_os/core/inventory.hpp>
#include <rpg_os/core/money.hpp>
#include <rpg_os/core/spellbook.hpp>
#include <rpg_os/universal/combat.hpp>
#include <rpg_os/universal/engine.hpp>
#include <rpg_os/universal/game_session.hpp>
#include <string>

using rpg_os::BookkeepingError;
using rpg_os::CheckParams;
using rpg_os::CurrencySystem;
using rpg_os::DynamicEntity;
using rpg_os::ItemInstance;
using rpg_os::Money;
using rpg_os::RulesetEngine;

/// A minimal D&D-style coinage for unit tests.
inline CurrencySystem dndCoins() {
  return CurrencySystem{"dnd",
                        "D&D",
                        "cp",
                        {{"cp", "Copper", "CP", 1},
                         {"sp", "Silver", "SP", 10},
                         {"gp", "Gold", "GP", 100},
                         {"pp", "Platinum", "PP", 1000}}};
}
