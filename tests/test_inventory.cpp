// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_inventory.cpp
 * @brief Inventory stacks, nested containers, and carrying weight.
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: Inventory adds, merges, and removes (phase 0)") {
  rpg_os::Inventory inv;
  inv.add(ItemInstance{"sword", 1, {}});
  inv.add(ItemInstance{"sword", 2, {}});
  CHECK(inv.count("sword") == 3);
  inv.add(ItemInstance{"potion", 5, {}});
  CHECK(inv.size() == 2);
  CHECK(inv.remove("sword", 2));
  CHECK(inv.count("sword") == 1);
  CHECK_FALSE(inv.remove("sword", 5)); // not enough -> unchanged
  CHECK(inv.count("sword") == 1);
  CHECK(inv.remove("sword", 1));
  CHECK_FALSE(inv.has("sword"));
}

TEST_CASE("bookkeeping: D&D encumbrance from weight and capacity (phase 2)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  const auto &ruleset = dnd.ruleset();
  DynamicEntity sheet(ruleset, "test");
  sheet.setBaseAttribute("STR", 10); // capacity = 15 * 10 = 150 lb

  REQUIRE(dnd.addItem(sheet, "club", 10).has_value()); // 10 * 2 lb = 20 lb
  CHECK(dnd.carriedWeight(sheet).value == doctest::Approx(20.0));
  const auto capacity = dnd.carryingCapacity(sheet);
  REQUIRE(capacity.has_value());
  CHECK(capacity->value == doctest::Approx(150.0));
  const auto level = dnd.encumbranceLevel(sheet);
  REQUIRE(level.has_value());
  CHECK(*level == 0);

  REQUIRE(dnd.addItem(sheet, "club", 70).has_value()); // 160 lb total
  const auto heavy = dnd.encumbranceLevel(sheet);
  REQUIRE(heavy.has_value());
  CHECK(*heavy == 2); // ratio 160/150 > 1.0 -> heaviest level
}

TEST_CASE("bookkeeping: containers hold items (bag-in-bags)") {
  const std::string rulesetJson = R"({
    "schema_version": 1, "ruleset_id": "mini", "licence": "test",
    "attributes": [{"id": "STR", "name": "Strength", "min": 1, "max": 30, "default": 10}],
    "encumbrance": {"weight_unit": "lb", "default_weight": 0.0, "capacity": "15 * STR", "levels": []},
    "data": {
      "items": [
        {"id": "backpack", "name": "Backpack", "weight": "5 lb."},
        {"id": "club", "name": "Club", "weight": "2 lb."}
      ]
    }
  })";
  RulesetEngine engine;
  REQUIRE(engine.loadRulesetFromJson(rulesetJson));
  const auto &ruleset = engine.ruleset();
  DynamicEntity sheet(ruleset, "test");
  sheet.setBaseAttribute("STR", 10);

  REQUIRE(engine.addItem(sheet, "backpack", 1).has_value());
  REQUIRE(engine.addItem(sheet, "club", 2).has_value());
  // flat count does not see what is inside the container...
  CHECK(sheet.inventory().count("club") == 2);
  CHECK(sheet.inventory().totalCount("club") == 2);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(9.0)); // 5 + 2*2

  // ...until we put a club into the backpack.
  REQUIRE(engine.addItemToContainer(sheet, "backpack", "club", 1).has_value());
  CHECK(sheet.inventory().countIn("backpack", "club") == 1);
  CHECK(sheet.inventory().totalCount("club") == 3);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(11.0)); // +2 in the pack
  REQUIRE(sheet.inventory().contentsOf("backpack") != nullptr);
  CHECK(sheet.inventory().contentsOf("backpack")->size() == 1);

  // a missing container is a distinct error
  CHECK(engine.addItemToContainer(sheet, "nope", "club", 1).error() ==
        BookkeepingError::UnknownContainer);

  // removing from the container returns the count to flat-only
  REQUIRE(engine.removeItemFromContainer(sheet, "backpack", "club", 1).has_value());
  CHECK(sheet.inventory().countIn("backpack", "club") == 0);
  CHECK(sheet.inventory().totalCount("club") == 2);
  CHECK(engine.carriedWeight(sheet).value == doctest::Approx(9.0));

  // serialization round-trips nested contents
  REQUIRE(engine.addItemToContainer(sheet, "backpack", "club", 1).has_value());
  rpg_os::Json saved;
  sheet.toJson(saved);
  DynamicEntity restored(ruleset, "test");
  restored.fromJson(saved);
  CHECK(restored.inventory().countIn("backpack", "club") == 1);
  CHECK(restored.inventory().totalCount("club") == 3);
}
