// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Smoke test: verifies that the doctest harness is wired up correctly so that
// the test executable builds, links, and runs.
#include <doctest/doctest.h>

TEST_CASE("smoke: test harness runs") {
  CHECK(1 + 1 == 2);
  CHECK_FALSE(1 + 1 == 3);
}
