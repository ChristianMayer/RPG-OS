// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared math helpers (rpg_os::math).
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
