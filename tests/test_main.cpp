// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Test driver for the rpg_os test suite.
//
// doctest is vendored under include/rpg_os/third_party/doctest/doctest.h and
// included as <doctest/doctest.h> via the rpg_os target's SYSTEM include
// directory. Exactly one translation unit defines the main().
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
