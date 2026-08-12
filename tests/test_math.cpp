// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_math.cpp
 * @brief Tests for the shared math helpers (@c rpg_os::math).
 *
 * These tests pin the exact numeric semantics of the formula function set
 * (floor/ceil/round/min/max/clamp and the integer conversions). Because both
 * the AST evaluator and the generated code call into @c rpg_os::math, this
 * file is the first line of defence for the cross-mode parity guarantee: if a
 * rounding convention ever changed, the universal and specific modes would
 * diverge here first.
 */
#include <doctest/doctest.h>
#include <rpg_os/core/math.hpp>

TEST_CASE("math: floor / ceil / round") {
  CHECK(rpg_os::math::floor(3.7) == doctest::Approx(3.0));
  CHECK(rpg_os::math::floor(-3.2) == doctest::Approx(-4.0));
  CHECK(rpg_os::math::ceil(3.2) == doctest::Approx(4.0));
  CHECK(rpg_os::math::ceil(-3.7) == doctest::Approx(-3.0));
  // std::round rounds half away from zero.
  CHECK(rpg_os::math::round(2.5) == doctest::Approx(3.0));
  CHECK(rpg_os::math::round(-2.5) == doctest::Approx(-3.0));
  CHECK(rpg_os::math::round(2.4) == doctest::Approx(2.0));
}

TEST_CASE("math: min / max") {
  CHECK(rpg_os::math::min(3.0, 7.0) == doctest::Approx(3.0));
  CHECK(rpg_os::math::max(3.0, 7.0) == doctest::Approx(7.0));
  CHECK(rpg_os::math::min(-1.0, 1.0) == doctest::Approx(-1.0));
}

TEST_CASE("math: clamp") {
  CHECK(rpg_os::math::clamp(5.0, 0.0, 3.0) == doctest::Approx(3.0));
  CHECK(rpg_os::math::clamp(-1.0, 0.0, 3.0) == doctest::Approx(0.0));
  CHECK(rpg_os::math::clamp(2.0, 0.0, 3.0) == doctest::Approx(2.0));
  CHECK(rpg_os::math::clampInt(5, 0, 3) == 3);
  CHECK(rpg_os::math::clampInt(-1, 0, 3) == 0);
  CHECK(rpg_os::math::clampInt(2, 0, 3) == 2);
}

TEST_CASE("math: toStat truncates toward zero") {
  CHECK(rpg_os::math::toStat(3.0) == 3);
  CHECK(rpg_os::math::toStat(2.99) == 2);
  CHECK(rpg_os::math::toStat(-2.99) == -2);
  CHECK(rpg_os::math::toStat(0.0) == 0);
}

TEST_CASE("math: minI / maxI") {
  CHECK(rpg_os::math::minI(3, 7) == 3);
  CHECK(rpg_os::math::maxI(3, 7) == 7);
  CHECK(rpg_os::math::minI(-1, 1) == -1);
  CHECK(rpg_os::math::maxI(-5, -3) == -3);
}

TEST_CASE("math: floorDiv matches floor of the rational") {
  CHECK(rpg_os::math::floorDiv(23, 2) == 11);   // floor(11.5)
  CHECK(rpg_os::math::floorDiv(22, 2) == 11);   // floor(11.0)
  CHECK(rpg_os::math::floorDiv(-23, 2) == -12); // floor(-11.5)
  CHECK(rpg_os::math::floorDiv(-22, 2) == -11); // floor(-11.0)
  CHECK(rpg_os::math::floorDiv(7, 5) == 1);     // floor(1.4)
  CHECK(rpg_os::math::floorDiv(3, 5) == 0);     // floor(0.6)
  CHECK(rpg_os::math::floorDiv(-3, 5) == -1);   // floor(-0.6)
  CHECK(rpg_os::math::floorDiv(23, -2) == -12); // floor(-11.5)
}

TEST_CASE("math: ceilDiv matches ceil of the rational") {
  CHECK(rpg_os::math::ceilDiv(23, 2) == 12);   // ceil(11.5)
  CHECK(rpg_os::math::ceilDiv(22, 2) == 11);   // ceil(11.0)
  CHECK(rpg_os::math::ceilDiv(-23, 2) == -11); // ceil(-11.5)
  CHECK(rpg_os::math::ceilDiv(-22, 2) == -11); // ceil(-11.0)
  CHECK(rpg_os::math::ceilDiv(3, 5) == 1);     // ceil(0.6)
  CHECK(rpg_os::math::ceilDiv(-3, 5) == 0);    // ceil(-0.6)
  CHECK(rpg_os::math::ceilDiv(23, -2) == -11); // ceil(-11.5)
}

TEST_CASE("math: roundDiv rounds half away from zero like std::round") {
  CHECK(rpg_os::math::roundDiv(23, 2) == 12);   // round(11.5)
  CHECK(rpg_os::math::roundDiv(22, 2) == 11);   // round(11.0)
  CHECK(rpg_os::math::roundDiv(-23, 2) == -12); // round(-11.5)
  CHECK(rpg_os::math::roundDiv(-22, 2) == -11); // round(-11.0)
  CHECK(rpg_os::math::roundDiv(25, 4) == 6);    // round(6.25)
  CHECK(rpg_os::math::roundDiv(26, 4) == 7);    // round(6.5)
  CHECK(rpg_os::math::roundDiv(27, 4) == 7);    // round(6.75)
  CHECK(rpg_os::math::roundDiv(-26, 4) == -7);  // round(-6.5)
  CHECK(rpg_os::math::roundDiv(23, -2) == -12); // round(-11.5)
}

TEST_CASE("math: non-negative divisions agree with the general forms") {
  // The fast *N variants are only valid for non-negative numerators; for those
  // inputs they must agree exactly with the general (negative-correct) forms.
  for (int a = 0; a <= 200; ++a) {
    for (int b = 1; b <= 12; ++b) {
      CHECK(rpg_os::math::floorDivN(a, b) == rpg_os::math::floorDiv(a, b));
      CHECK(rpg_os::math::ceilDivN(a, b) == rpg_os::math::ceilDiv(a, b));
      CHECK(rpg_os::math::roundDivN(a, b) == rpg_os::math::roundDiv(a, b));
    }
  }
}

TEST_CASE("math: helpers are constant expressions (compile-time evaluation)") {
  // Every pure helper is constexpr, so the compiler can fold literal calls at
  // compile time — the generated specific-mode code relies on this to collapse
  // its formula arithmetic into constants where possible.
  static_assert(rpg_os::math::floorDiv(23, 2) == 11);
  static_assert(rpg_os::math::ceilDiv(23, 2) == 12);
  static_assert(rpg_os::math::roundDiv(23, 2) == 12);
  static_assert(rpg_os::math::floorDivN(23, 2) == 11);
  static_assert(rpg_os::math::ceilDivN(23, 2) == 12);
  static_assert(rpg_os::math::roundDivN(23, 2) == 12);
  static_assert(rpg_os::math::minI(3, 7) == 3);
  static_assert(rpg_os::math::maxI(3, 7) == 7);
  static_assert(rpg_os::math::clampInt(5, 0, 3) == 3);
  static_assert(rpg_os::math::toStat(2.99) == 2);
  static_assert(rpg_os::math::floor(3.7) == 3.0);
  static_assert(rpg_os::math::round(2.5) == 3.0);
}
