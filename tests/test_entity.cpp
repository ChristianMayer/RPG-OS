// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared entity helpers (rpg_os::ResourcePool).
#include <doctest/doctest.h>
#include <rpg_os/core/entity.hpp>

TEST_CASE("ResourcePool: modifies within bounds") {
  rpg_os::ResourcePool hp{10, 0, 20};
  CHECK(hp.modify(5) == 5);
  CHECK(hp.current == 15);
  CHECK(hp.modify(-6) == -6);
  CHECK(hp.current == 9);
}

TEST_CASE("ResourcePool: clamps at the minimum") {
  rpg_os::ResourcePool hp{3, 0, 20};
  CHECK(hp.modify(-10) == -3); // only 3 actually applied
  CHECK(hp.current == 0);
}

TEST_CASE("ResourcePool: clamps at the maximum") {
  rpg_os::ResourcePool hp{18, 0, 20};
  CHECK(hp.modify(10) == 2); // only 2 actually applied
  CHECK(hp.current == 20);
}

TEST_CASE("ResourcePool: already at a bound stays there") {
  rpg_os::ResourcePool hp{0, 0, 20};
  CHECK(hp.modify(-5) == 0);
  CHECK(hp.current == 0);
  CHECK(hp.modify(25) == 20);
  CHECK(hp.current == 20);
  CHECK(hp.modify(1) == 0);
}
