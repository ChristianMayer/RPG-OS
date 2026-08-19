// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_money.cpp
 * @brief Money, currency conversion, and the economy (prices, buying, paying).
 */
#include "test_fixtures.hpp"

TEST_CASE("bookkeeping: Money stores base units and converts (phase 0)") {
  const CurrencySystem coins = dndCoins();
  const Money twoGold = Money::fromCoins(coins, {{"gp", 2}});
  CHECK(twoGold.baseUnits() == 200);
  const Money bag = Money::fromCoins(coins, {{"gp", 2}, {"sp", 5}, {"cp", 3}});
  CHECK(bag.baseUnits() == 253);
  CHECK(bag.in(coins, "gp") == 2);
  CHECK(bag.in(coins, "sp") == 25);
  CHECK((twoGold + bag).baseUnits() == 453);
  CHECK((bag - twoGold).baseUnits() == 53);
  CHECK((bag * 3).baseUnits() == 759);
  CHECK(twoGold.baseUnits() < bag.baseUnits());
  CHECK_FALSE(twoGold.baseUnits() > bag.baseUnits());
  CHECK_THROWS_AS(static_cast<void>(Money::fromCoins(coins, {{"nope", 1}})), std::invalid_argument);
  CHECK(coins.valueOf("gp", 3) == 300);
  CHECK(coins.valueOf("nope", 1) == 0);
}

TEST_CASE("bookkeeping: itemPrice parses D&D cost strings (phase 1)") {
  RulesetEngine dnd;
  REQUIRE(dnd.loadRulesetFromFile(rulesetPath("dnd5e_srd.json")));
  REQUIRE(dnd.hasCurrency());
  const auto club = dnd.itemPrice("club"); // "1 SP"
  REQUIRE(club.has_value());
  CHECK(club->baseUnits() == 10); // 1 silver = 10 copper
  CHECK(dnd.itemPrice("nope").error() == BookkeepingError::UnknownItem);
}

TEST_CASE("bookkeeping: TDE prices use Silbertaler (phase 1)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  const auto dagger = tde.itemPrice("dagger"); // "45 ST"
  REQUIRE(dagger.has_value());
  CHECK(dagger->baseUnits() == 4500); // 45 * 100 heller
  CHECK(dagger->in(tde.currencySystem(), "silbertaler") == 45);
  CHECK(dagger->in(tde.currencySystem(), "dukat") == 22); // 4500 / 200
  // a ruleset without money has no prices
  RulesetEngine brp;
  REQUIRE(brp.loadRulesetFromFile(rulesetPath("brp_ugc.json")));
  CHECK_FALSE(brp.hasCurrency());
  CHECK(brp.itemPrice("dagger").error() == BookkeepingError::UnknownCurrency);
}

TEST_CASE("bookkeeping: buy and pay transfer money and items (phase 1)") {
  RulesetEngine tde;
  REQUIRE(tde.loadRulesetFromFile(rulesetPath("tde5e_core.json")));
  auto geron = tde.createEntity("geron");
  REQUIRE(geron != nullptr);
  CHECK(geron->money().baseUnits() == 2500); // 25 ST starting wealth
  geron->money() = Money{50000};

  const auto total = tde.buy(*geron, nullptr, "dagger", 2);
  REQUIRE(total.has_value());
  CHECK(total->baseUnits() == 9000); // 2 * 45 ST
  CHECK(geron->money().baseUnits() == 50000 - 9000);
  CHECK(geron->inventory().count("dagger") == 2);

  // insufficient funds fail without side effects
  geron->money() = Money{10};
  const auto poor = tde.buy(*geron, nullptr, "dagger", 1);
  CHECK(poor.error() == BookkeepingError::NotEnoughMoney);
  CHECK(geron->inventory().count("dagger") == 2);
  CHECK(geron->money().baseUnits() == 10);

  // pay transfers between two sheets
  geron->money() = Money{1000};
  auto other = tde.createEntity("geron");
  REQUIRE(other != nullptr);
  REQUIRE(tde.pay(*geron, other.get(), Money{400}).has_value());
  CHECK(geron->money().baseUnits() == 600);
  CHECK(other->money().baseUnits() == 2500 + 400);
  CHECK(tde.pay(*geron, nullptr, Money{100000}).error() == BookkeepingError::NotEnoughMoney);
}
