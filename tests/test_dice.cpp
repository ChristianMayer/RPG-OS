// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_dice.cpp
 * @brief Tests for the shared dice engine (@c rpg_os::DiceExpression,
 * @c rpg_os::DefaultRandom).
 *
 * Pins expression parsing (groups, constants, signs, bare d20), rolling via
 * scripted RNGs, the user-defined literal forms — the @c "2d6+4"_dice suffix
 * for arbitrary sizes and the @c 2_d6 / @c 1_d20 die-size suffixes with
 * arithmetic (@c 2_d6+2) — the malformed-input error contract, and the RNG
 * seeding contract: a fixed seed is deterministic, while unseeded instances
 * draw easily available entropy. These tests are the reason scripted-RNG
 * usage stays exact in the rest of the suite.
 */
#include "test_util.hpp"

#include <cstddef>
#include <doctest/doctest.h>
#include <rpg_os/core/dice_engine.hpp>
#include <stdexcept>
#include <vector>

using namespace rpg_os::dice_literals;

namespace {
/// Number of faces of an expression's first die group. A free function (not a
/// method call on the literal) so the tests can write `dieSides(1_d6)` — a
/// bare `1_d6.dice()` would be swallowed into one literal token (`.` is a
/// valid pp-number character).
int dieSides(const rpg_os::DiceExpression &expr) {
  return expr.dice()[0].sides;
}
} // namespace

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

TEST_CASE("DiceExpression: string literal builds a real parsed object") {
  const auto expr = "2d6+4"_dice;
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 2);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.dice()[0].sign == 1);
  CHECK(expr.constant() == 4);
  auto r1 = script({3, 5});
  CHECK(expr.roll(r1) == std::vector<int>{3, 5});
  auto r2 = script({4, 2});
  CHECK(expr.rollSum(r2) == 10);
}

TEST_CASE("DiceExpression: integer literal builds a constant-only expression") {
  const auto expr = 7_dice;
  CHECK(expr.dice().empty());
  CHECK(expr.constant() == 7);
  auto rng = script({});
  CHECK(expr.rollSum(rng) == 7);
  CHECK(expr.roll(rng).empty());
}

TEST_CASE("DiceExpression: integer constructor builds a constant-only expression") {
  const rpg_os::DiceExpression expr(7);
  CHECK(expr.dice().empty());
  CHECK(expr.constant() == 7);
  auto rng = script({});
  CHECK(expr.rollSum(rng) == 7);
}

TEST_CASE("DiceExpression: literals agree with the string constructor") {
  const rpg_os::DiceExpression fromLiteral = "d20"_dice;
  const rpg_os::DiceExpression fromCtor("d20");
  REQUIRE(fromLiteral.dice().size() == fromCtor.dice().size());
  CHECK(fromLiteral.dice()[0].count == fromCtor.dice()[0].count);
  CHECK(fromLiteral.dice()[0].sides == fromCtor.dice()[0].sides);
  CHECK(fromLiteral.dice()[0].sign == fromCtor.dice()[0].sign);
  CHECK(fromLiteral.constant() == fromCtor.constant());
}

TEST_CASE("DiceExpression: bare d20 literal means one d20") {
  const auto expr = "d20"_dice;
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 1);
  CHECK(expr.dice()[0].sides == 20);
  auto rng = script({5});
  CHECK(expr.roll(rng) == std::vector<int>{5});
}

TEST_CASE("DiceExpression: die-size literals build a single group") {
  const auto expr = 3_d20;
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 3);
  CHECK(expr.dice()[0].sides == 20);
  CHECK(expr.dice()[0].sign == 1);
  CHECK(expr.constant() == 0);
  auto r1 = script({4, 10, 16});
  CHECK(expr.roll(r1) == std::vector<int>{4, 10, 16});
  auto r2 = script({1, 2, 3});
  CHECK(expr.rollSum(r2) == 6);
}

TEST_CASE("DiceExpression: every common die size has a suffix") {
  CHECK(dieSides(1_d2) == 2);
  CHECK(dieSides(1_d3) == 3);
  CHECK(dieSides(1_d4) == 4);
  CHECK(dieSides(1_d6) == 6);
  CHECK(dieSides(1_d8) == 8);
  CHECK(dieSides(1_d10) == 10);
  CHECK(dieSides(1_d12) == 12);
  CHECK(dieSides(1_d20) == 20);
  CHECK(dieSides(1_d30) == 30);
  CHECK(dieSides(1_d100) == 100);
}

TEST_CASE("DiceExpression: adding an integer appends a constant") {
  const auto expr = 2_d6 + 2;
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 2);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.constant() == 2);
  auto r1 = script({3, 5});
  CHECK(expr.roll(r1) == std::vector<int>{3, 5});
  auto r2 = script({3, 5});
  CHECK(expr.rollSum(r2) == 10);
}

TEST_CASE("DiceExpression: adding expressions combines groups") {
  const auto expr = 2_d6 + 1_d4;
  REQUIRE(expr.dice().size() == 2);
  CHECK(expr.dice()[0].count == 2);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.dice()[0].sign == 1);
  CHECK(expr.dice()[1].count == 1);
  CHECK(expr.dice()[1].sides == 4);
  CHECK(expr.dice()[1].sign == 1);
  auto rng = script({1, 2, 3});
  CHECK(expr.roll(rng) == std::vector<int>{1, 2, 3});
}

TEST_CASE("DiceExpression: subtraction negates the right side") {
  const auto expr = 1_d6 - 1_d4;
  REQUIRE(expr.dice().size() == 2);
  CHECK(expr.dice()[0].sign == 1);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.dice()[1].count == 1);
  CHECK(expr.dice()[1].sides == 4);
  CHECK(expr.dice()[1].sign == -1);
  auto r1 = script({6, 1});
  CHECK(expr.roll(r1) == std::vector<int>{6, 1}); // raw rolls stay positive
  auto r2 = script({6, 1});
  CHECK(expr.rollSum(r2) == 5);
}

TEST_CASE("DiceExpression: subtracting an integer") {
  const auto expr = 1_d20 - 2;
  CHECK(expr.constant() == -2);
  auto rng = script({10});
  CHECK(expr.rollSum(rng) == 8);
}

TEST_CASE("DiceExpression: integer on the left of + and -") {
  const auto plus = 2 + 2_d6;
  CHECK(plus.constant() == 2);
  REQUIRE(plus.dice().size() == 1);
  CHECK(plus.dice()[0].count == 2);
  CHECK(plus.dice()[0].sides == 6);
  auto rngPlus = script({3, 4});
  CHECK(plus.rollSum(rngPlus) == 9);

  const auto minus = 2 - 2_d6;
  CHECK(minus.constant() == 2);
  REQUIRE(minus.dice().size() == 1);
  CHECK(minus.dice()[0].count == 2);
  CHECK(minus.dice()[0].sign == -1);
  auto rngMinus = script({3, 4});
  CHECK(minus.rollSum(rngMinus) == -5);
}

TEST_CASE("DiceExpression: unary minus negates") {
  const auto expr = -2_d6;
  REQUIRE(expr.dice().size() == 1);
  CHECK(expr.dice()[0].count == 2);
  CHECK(expr.dice()[0].sides == 6);
  CHECK(expr.dice()[0].sign == -1);
  CHECK(expr.constant() == 0);
  auto rng = script({2, 5});
  CHECK(expr.rollSum(rng) == -7);
}

TEST_CASE("DiceExpression: die-size literals agree with the string form") {
  const auto fromLiterals = 2_d6 + 2;
  const rpg_os::DiceExpression fromString("2d6+2");
  REQUIRE(fromLiterals.dice().size() == fromString.dice().size());
  CHECK(fromLiterals.dice()[0].count == fromString.dice()[0].count);
  CHECK(fromLiterals.dice()[0].sides == fromString.dice()[0].sides);
  CHECK(fromLiterals.dice()[0].sign == fromString.dice()[0].sign);
  CHECK(fromLiterals.constant() == fromString.constant());
}

TEST_CASE("DiceExpression: die-size literals compose with the _dice suffix") {
  const auto expr = "1d7"_dice + 2_d6;
  REQUIRE(expr.dice().size() == 2);
  CHECK(expr.dice()[0].sides == 7);
  CHECK(expr.dice()[0].count == 1);
  CHECK(expr.dice()[1].sides == 6);
  CHECK(expr.dice()[1].count == 2);
  CHECK(expr.constant() == 0);
}

TEST_CASE("DefaultRandom: deterministic for a fixed seed") {
  rpg_os::DefaultRandom a(42u);
  rpg_os::DefaultRandom b(42u);
  for (int i = 0; i < 10; ++i) {
    CHECK(a(1, 6) == b(1, 6));
  }
}

TEST_CASE("DefaultRandom: unseeded instances draw varied entropy") {
  // Two independent default-seeded engines must not share a stream: each
  // draws its own seed from the OS random device, so a 32-bit collision is
  // essentially impossible and a difference within a few draws is the
  // expected (and stable) outcome.
  rpg_os::DefaultRandom a;
  rpg_os::DefaultRandom b;
  bool anyDifference = false;
  for (int i = 0; i < 16; ++i) {
    if (a(1, 1000) != b(1, 1000)) {
      anyDifference = true;
      break;
    }
  }
  CHECK(anyDifference);
}

TEST_CASE("randomSeed: yields varied values") {
  const uint32_t first = rpg_os::randomSeed();
  const uint32_t second = rpg_os::randomSeed();
  const uint32_t third = rpg_os::randomSeed();
  const bool varied = (first != second) || (first != third) || (second != third);
  CHECK(varied);
}
