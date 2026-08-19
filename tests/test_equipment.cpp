// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_equipment.cpp
 * @brief Equipment slots, equipping/unequipping, and gear-modified effective stats.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: Equipment slots hold one item each (phase 0)") {
  rpg_os::Equipment eq;
  CHECK(eq.equip("weapon_hand", "sword").empty());
  CHECK(eq.isEquipped("weapon_hand"));
  CHECK(eq.itemIn("weapon_hand") == "sword");
  CHECK(eq.equip("weapon_hand", "axe") == "sword"); // overwrite returns old
  CHECK(eq.itemIn("weapon_hand") == "axe");
  CHECK(eq.unequip("weapon_hand") == "axe");
  CHECK_FALSE(eq.isEquipped("weapon_hand"));
  CHECK(eq.unequip("weapon_hand").empty());
}

TEST_CASE("bookkeeping: equip and unequip move items between slots (phase 2)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto sheet = tde.createEntity("geron");
  REQUIRE(sheet != nullptr);
  // geron starts with a longsword equipped
  CHECK(sheet->equipment().isEquipped("weapon_hand"));
  CHECK(sheet->equipment().itemIn("weapon_hand") == "longsword");

  // cannot equip what you do not own
  CHECK(tde.equip(*sheet, "weapon_hand", "dagger").error() == BookkeepingError::ItemNotOwned);

  REQUIRE(tde.addItem(*sheet, "dagger", 1).has_value());
  const auto equipped = tde.equip(*sheet, "weapon_hand", "dagger");
  REQUIRE(equipped.has_value());
  CHECK(equipped->previousItemId == "longsword"); // swap
  CHECK(sheet->equipment().itemIn("weapon_hand") == "dagger");
  CHECK(sheet->inventory().count("longsword") == 1); // old item returned
  CHECK(sheet->inventory().count("dagger") == 0);

  // unequip returns the item
  REQUIRE(tde.unequip(*sheet, "weapon_hand").has_value());
  CHECK_FALSE(sheet->equipment().isEquipped("weapon_hand"));
  CHECK(sheet->inventory().count("dagger") == 1);

  // slot / item mismatches fail
  CHECK(tde.equip(*sheet, "weapon_hand", "leather_armor").error() ==
        BookkeepingError::SlotMismatch);
  CHECK(tde.equip(*sheet, "no_such_slot", "dagger").error() == BookkeepingError::SlotMismatch);
  CHECK(tde.unequip(*sheet, "weapon_hand").error() == BookkeepingError::NotEquipped);
}

TEST_CASE("bookkeeping: equipped gear modifies effective stats (phase 2)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto sheet = tde.createEntity("geron");
  REQUIRE(sheet != nullptr);
  // geron base Armor_Rating = 2; leather armor adds +3
  CHECK(sheet->baseAttribute("Armor_Rating") == 2);
  CHECK(sheet->getStat("Armor_Rating") == 2); // raw stays raw
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 5);

  REQUIRE(tde.unequip(*sheet, "body_armor").has_value());
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 2);
  REQUIRE(tde.equip(*sheet, "body_armor", "leather_armor").has_value());
  CHECK(sheet->getEffectiveStat("Armor_Rating") == 5);
}

TEST_CASE("bookkeeping: effective armour reduces damage in the pipeline") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  rpg_os::DynamicEntity orc(tde.ruleset(), "orc");

  // geron wears leather armour: raw Armor_Rating 2, effective 5.
  CHECK(geron->baseAttribute("Armor_Rating") == 2);
  CHECK(geron->getEffectiveStat("Armor_Rating") == 5);
  const int32_t lpBefore = geron->resource("LP");
  CHECK(tde.applyDamage(orc, *geron, "LP", 10) == -5); // 10 - effective 5

  // without the armour only the raw rating applies
  REQUIRE(tde.unequip(*geron, "body_armor").has_value());
  const int32_t lpAfter = geron->resource("LP");
  CHECK(tde.applyDamage(orc, *geron, "LP", 10) == -8); // 10 - raw 2
  CHECK(geron->resource("LP") == lpBefore - 5 - 8);
  CHECK(geron->resource("LP") == lpAfter - 8);
}
