// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_smoke.cpp
 * @brief Smoke test for the doctest harness.
 *
 * Verifies that the test executable builds, links, and runs at all. This is
 * deliberately trivial: if the vendored doctest, the CMake wiring, or the
 * test runner regress, this single case fails first — isolating infrastructure
 * breakage from actual rule-logic failures.
 */
#include <doctest/doctest.h>

TEST_CASE("smoke: test harness runs") {
  CHECK(1 + 1 == 2);
  CHECK_FALSE(1 + 1 == 3);
}
