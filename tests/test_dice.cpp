// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared dice engine (rpg_os::DiceExpression, rpg_os::DefaultRandom).
#include "test_util.hpp"

#include <cstddef>
#include <doctest/doctest.h>
#include <rpg_os/core/dice_engine.hpp>
#include <stdexcept>
#include <vector>

TEST_CASE("DiceExpression: single die roll") {
  const rpg_os::DiceExpression expr("1d20");
  auto r1 = script({14});
  CHECK(expr.roll(r1) == std::vector<int>{14});
  auto r2 = script({10});
  CHECK(expr.rollSum(r2) == 10);
}

TEST_CASE("DiceExpression: stays within range over many rolls") {
  const rpg_os::DiceExpression expr("1d20");
  rpg_os::DefaultRandom rng(12345u);
  int minRoll = 21;
  int maxRoll = 0;
  for (int i = 0; i < 5000; ++i) {
    const int value = expr.rollSum(rng);
    minRoll = std::min(minRoll, value);
    maxRoll = std::max(maxRoll, value);
  }
  CHECK(minRoll == 1);
  CHECK(maxRoll == 20);
}

TEST_CASE("DiceExpression: sums with a flat constant") {
  const rpg_os::DiceExpression expr("2d6+4");
  auto r1 = script({3, 5});
  CHECK(expr.roll(r1) == std::vector<int>{3, 5});
  auto r2 = script({4, 2});
  CHECK(expr.rollSum(r2) == 10);
}

TEST_CASE("DiceExpression: constant-only expression") {
  const rpg_os::DiceExpression expr("7");
  CHECK(expr.dice().empty());
  CHECK(expr.constant() == 7);
  auto rng = script({});
  CHECK(expr.rollSum(rng) == 7);
  CHECK(expr.roll(rng).empty());
}

TEST_CASE("DiceExpression: negative constant") {
  const rpg_os::DiceExpression expr("1d20-2");
  auto rng = script({10});
  CHECK(expr.rollSum(rng) == 8);
  CHECK(expr.constant() == -2);
}

TEST_CASE("DiceExpression: bare d20 means one d20") {
  const rpg_os::DiceExpression expr("d20");
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 1);
  CHECK(expr.dice()[0].sides == 20);
  auto rng = script({5});
  CHECK(expr.roll(rng) == std::vector<int>{5});
}

TEST_CASE("DiceExpression: multiple die groups") {
  const rpg_os::DiceExpression expr("2d6+1d4");
  auto r1 = script({1, 2, 3});
  CHECK(expr.roll(r1) == std::vector<int>{1, 2, 3});
  auto r2 = script({1, 1, 2});
  CHECK(expr.rollSum(r2) == 4);
}

TEST_CASE("DiceExpression: negative die group subtracts from the sum") {
  const rpg_os::DiceExpression expr("1d6-1d6");
  auto r1 = script({6, 1});
  CHECK(expr.roll(r1) == std::vector<int>{6, 1}); // raw rolls stay positive
  auto r2 = script({6, 1});
  CHECK(expr.rollSum(r2) == 5);
}

TEST_CASE("DiceExpression: exposes parsed structure") {
  const rpg_os::DiceExpression expr("2d6+1d4-3");
  REQUIRE(expr.dice().size() == 2);
  CHECK(expr.dice()[0].count == 2);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.dice()[0].sign == 1);
  CHECK(expr.dice()[1].count == 1);
  CHECK(expr.dice()[1].sides == 4);
  CHECK(expr.constant() == -3);
}

TEST_CASE("DiceExpression: malformed expressions throw") {
  CHECK_THROWS_AS(rpg_os::DiceExpression(""), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("d"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("1d"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("abc"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("2d6+"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("3d0"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::DiceExpression("0d6"), std::invalid_argument);
}

TEST_CASE("DefaultRandom: deterministic for a fixed seed") {
  rpg_os::DefaultRandom a(42u);
  rpg_os::DefaultRandom b(42u);
  for (int i = 0; i < 10; ++i) {
    CHECK(a(1, 6) == b(1, 6));
  }
}
