// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_types.cpp
 * @brief Unit tests for the shared value types (@c rpg_os::common/types.hpp).
 *
 * @c CheckResult and @c SuccessLevel are the contract both modes produce and
 * consume; these tests pin their defaults and the ordering the percentile
 * (d100) grading relies on, so neither can silently change shape.
 */
#include <doctest/doctest.h>
#include <rpg_os/common/types.hpp>
#include <type_traits>

TEST_CASE("types: StatValue is a signed 32-bit integer") {
  static_assert(std::is_same_v<rpg_os::StatValue, int32_t>);
  CHECK(sizeof(rpg_os::StatValue) == 4);
}

TEST_CASE("types: SuccessLevel is ordered ascending for level comparison") {
  using rpg_os::SuccessLevel;
  CHECK(static_cast<int>(SuccessLevel::Fumble) < static_cast<int>(SuccessLevel::Failure));
  CHECK(static_cast<int>(SuccessLevel::Failure) < static_cast<int>(SuccessLevel::Success));
  CHECK(static_cast<int>(SuccessLevel::Success) < static_cast<int>(SuccessLevel::Special));
  CHECK(static_cast<int>(SuccessLevel::Special) < static_cast<int>(SuccessLevel::Critical));
}

TEST_CASE("types: CheckResult defaults to the neutral state") {
  rpg_os::CheckResult result;
  CHECK_FALSE(result.isSuccess);
  CHECK_FALSE(result.isCriticalSuccess);
  CHECK_FALSE(result.isCriticalFailure);
  CHECK(result.marginOfSuccess == 0);
  CHECK(result.rawDiceRolls.empty());
  CHECK(result.logDescription.empty());
  CHECK(result.remainingPool == 0);
  CHECK(result.qualityLevel == 0);
  CHECK(result.successLevel == rpg_os::SuccessLevel::Failure);
}
