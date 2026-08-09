// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Shared check resolution algorithms.
//
// The check algorithms (additive d20, roll-under d20, DSA 3d20 pool, and
// attack-vs-defense) live here as templates over a `StatProvider` and an RNG.
// Both modes call the very same functions:
//   - generated specific-mode code calls them directly with string literals
//     for every parameter, so the whole computation is inlined and dead
//     branches fold away (zero dynamic allocation in the hot path);
//   - the universal engine feeds them through `CheckConfig` (owning
//     std::string) via the `resolveCheck` dispatcher.
//
// `CheckParams` is a mode-independent per-call value type.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <rpg_os/common/types.hpp>
#include <rpg_os/core/concepts.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rpg_os {

/// The supported check mechanisms.
enum class CheckKind {
  AdditiveD20,         ///< 1d20 + bonuses >= threshold (D&D attack / ability check)
  RollUnderD20,        ///< 1d20 <= effective attribute (DSA attribute check)
  TripleRollUnderPool, ///< 3d20 vs three attributes, pool compensates (DSA talent)
  AttackVsDefense,     ///< attacker vs. defender roll-under contest (DSA combat)
};

/// Describes HOW a check is resolved, as loaded from a ruleset's `check_types`.
/// Owns its strings, so it has no lifetime dependencies on the ruleset.
struct CheckConfig {
  CheckKind kind{CheckKind::AdditiveD20};

  /// AdditiveD20: the dice expression (e.g. "1d20").
  std::string diceExpression{"1d20"};

  /// AdditiveD20: actor stats summed into the total (e.g. STR_mod, proficiency).
  std::vector<std::string> bonusStats;
  /// AdditiveD20: stat on the target used as the threshold; empty => the DC is
  /// taken from CheckParams::difficulty.
  std::string targetStat{};

  /// RollUnderD20 / TripleRollUnderPool: the attribute ids to check against.
  std::array<std::string, 3> attributes{};
  std::size_t numAttributes{0};
  /// TripleRollUnderPool: the pool stat id (e.g. the skill rating).
  std::string poolStat{};
  /// RollUnderD20: roll again on a natural 1 / 20 to confirm criticals and
  /// botches (The Dark Eye default); when false, nat 1 / nat 20 are direct.
  bool useConfirmationRoll{false};

  /// AttackVsDefense: stat ids for the attack (actor) and parry/dodge (target).
  std::string attackStat{};
  std::string parryStat{};
};

/// Per-call inputs shared by both modes.
struct CheckParams {
  /// AdditiveD20: the DC when config.targetStat is empty; RollUnderD20: the
  /// attribute modifier; TripleRollUnderPool / AttackVsDefense: the modifier
  /// subtracted from the pool / attack value.
  int32_t difficulty{0};
  /// Extra flat modifier added to the d20 total (AdditiveD20) or the pool.
  int32_t situationalModifier{0};
};

/// The Dark Eye quality level derived from leftover skill points:
/// 0-3 -> 1, 4-6 -> 2, 7-9 -> 3, 10-12 -> 4, 13-15 -> 5, 16+ -> 6.
[[nodiscard]] inline int32_t qualityLevelFromRemaining(int32_t remaining) noexcept {
  if (remaining <= 0) {
    return 1;
  }
  return (remaining - 1) / 3 + 1;
}

namespace detail {

template <RandomNumberGenerator Rng> inline int rollDie(int sides, Rng &rng) {
  return rng(1, sides);
}

} // namespace detail

/// Additive d20: total = rolled dice + bonus stats + situational modifier,
/// compared to the target's stat (or params.difficulty). Natural 20 always
/// succeeds, natural 1 always fails.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] CheckResult
resolveAdditiveD20(const Actor &actor, const Target &target, std::string_view diceExpression,
                   std::span<const std::string_view> bonusStats, std::string_view targetStat,
                   const CheckParams &params, Rng &rng) {
  CheckResult result;
  result.rawDiceRolls = DiceExpression(diceExpression).roll(rng);
  int total = params.situationalModifier;
  for (const std::string_view stat : bonusStats) {
    total += actor.getStat(stat);
  }
  for (const int die : result.rawDiceRolls) {
    total += die;
  }
  const int targetValue = targetStat.empty() ? params.difficulty : target.getStat(targetStat);
  result.marginOfSuccess = static_cast<int32_t>(total - targetValue);

  const bool natural20 = !result.rawDiceRolls.empty() && result.rawDiceRolls.front() == 20;
  const bool natural1 = !result.rawDiceRolls.empty() && result.rawDiceRolls.front() == 1;
  result.isCriticalSuccess = natural20;
  result.isCriticalFailure = natural1;
  if (natural20) {
    result.isSuccess = true;
  } else if (natural1) {
    result.isSuccess = false;
  } else {
    result.isSuccess = total >= targetValue;
  }
  return result;
}

/// Roll-under d20: 1d20 <= effective attribute (attribute + difficulty).
/// Optional confirmation rolls for criticals (nat 1) and botches (nat 20).
template <StatProvider Actor, RandomNumberGenerator Rng>
[[nodiscard]] CheckResult resolveRollUnderD20(const Actor &actor, std::string_view attribute,
                                              bool useConfirmationRoll, const CheckParams &params,
                                              Rng &rng) {
  CheckResult result;
  const int32_t eav = actor.getStat(attribute) + params.difficulty;
  if (eav < 1) {
    // Cannot roll below 1, so success is impossible.
    result.isSuccess = false;
    return result;
  }
  const int roll = detail::rollDie(20, rng);
  result.rawDiceRolls.push_back(roll);

  if (useConfirmationRoll && (roll == 1 || roll == 20)) {
    const int confirm = detail::rollDie(20, rng);
    result.rawDiceRolls.push_back(confirm);
    if (roll == 1) {
      // A successful confirmation is a critical success; otherwise a regular
      // success on the original check.
      result.isSuccess = true;
      result.isCriticalSuccess = confirm <= eav;
    } else {
      // A failed confirmation (or another 20) is a botch; otherwise a regular
      // failure on the original check.
      result.isSuccess = false;
      result.isCriticalFailure = confirm > eav || confirm == 20;
    }
  } else {
    result.isSuccess = roll <= eav;
    result.isCriticalSuccess = roll == 1;
    result.isCriticalFailure = roll == 20;
  }
  result.marginOfSuccess = static_cast<int32_t>(eav - roll);
  return result;
}

/// The Dark Eye skill check (3d20): roll three d20s against three linked
/// attributes. The difficulty modifier is applied to all three effective
/// attribute values (EAV = attribute + difficulty; a positive value is a
/// bonus, a negative one a penalty). Each roll above its EAV must be
/// compensated from the skill points (pool = skill rating), i.e. the overshoot
/// is subtracted from the pool. The check succeeds while the pool does not go
/// negative. If any EAV drops below 1 the check fails automatically. Double-1
/// is a critical success, double-20 a critical failure.
template <StatProvider Actor, RandomNumberGenerator Rng>
[[nodiscard]] CheckResult resolveTripleRollUnderPool(const Actor &actor, std::string_view attr1,
                                                     std::string_view attr2, std::string_view attr3,
                                                     std::string_view poolStat,
                                                     const CheckParams &params, Rng &rng) {
  CheckResult result;
  result.rawDiceRolls.reserve(3);

  const std::array<std::string_view, 3> attributes{attr1, attr2, attr3};
  int32_t eav[3]{};
  for (std::size_t i = 0; i < 3; ++i) {
    eav[i] = actor.getStat(attributes[i]) + params.difficulty;
    if (eav[i] < 1) {
      // Cannot roll below 1: this part of the check can never pass.
      result.isSuccess = false;
      return result;
    }
  }

  int32_t dice[3]{};
  int32_t totalMargin = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    dice[i] = detail::rollDie(20, rng);
    result.rawDiceRolls.push_back(dice[i]);
    totalMargin += dice[i] > eav[i] ? (dice[i] - eav[i]) : 0;
  }

  const int32_t pool = actor.getStat(poolStat) + params.situationalModifier;
  const int32_t finalPool = pool - totalMargin;
  result.remainingPool = finalPool;
  result.marginOfSuccess = finalPool;

  const bool doubleOne = (dice[0] == 1 && dice[1] == 1) || (dice[0] == 1 && dice[2] == 1) ||
                         (dice[1] == 1 && dice[2] == 1);
  const bool doubleTwenty = (dice[0] == 20 && dice[1] == 20) || (dice[0] == 20 && dice[2] == 20) ||
                            (dice[1] == 20 && dice[2] == 20);
  result.isCriticalSuccess = doubleOne;
  result.isCriticalFailure = doubleTwenty;
  result.isSuccess = finalPool >= 0;
  if (doubleOne) {
    result.isSuccess = true;
  } else if (doubleTwenty) {
    result.isSuccess = false;
  }
  result.qualityLevel = result.isSuccess ? qualityLevelFromRemaining(finalPool) : 0;
  return result;
}

/// DSA combat: the attacker rolls against the attack value; on success the
/// defender may roll against the parry value. The attack hits when the attacker
/// succeeds and the defender fails. Natural 1s are critical successes, natural
/// 20s are fumbles.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] CheckResult
resolveAttackVsDefense(const Actor &actor, const Target &target, std::string_view attackStat,
                       std::string_view parryStat, const CheckParams &params, Rng &rng) {
  CheckResult result;
  const int32_t attackValue = actor.getStat(attackStat) + params.difficulty;
  const int32_t parryValue = target.getStat(parryStat);

  const int attackRoll = detail::rollDie(20, rng);
  result.rawDiceRolls.push_back(attackRoll);
  result.isCriticalSuccess = attackRoll == 1;
  result.isCriticalFailure = attackRoll == 20;
  const bool attackHits =
      attackRoll == 1 ? true : (attackRoll == 20 ? false : attackRoll <= attackValue);
  result.marginOfSuccess = static_cast<int32_t>(attackValue - attackRoll);

  if (!attackHits) {
    result.isSuccess = false;
    return result;
  }

  const int parryRoll = detail::rollDie(20, rng);
  result.rawDiceRolls.push_back(parryRoll);
  const bool parrySucceeds =
      parryRoll == 1 ? true : (parryRoll == 20 ? false : parryRoll <= parryValue);
  result.isSuccess = !parrySucceeds;
  return result;
}

/// Runtime dispatcher over CheckKind. Specific mode calls the algorithm
/// functions directly instead.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] CheckResult resolveCheck(const Actor &actor, const Target &target,
                                       const CheckConfig &config, const CheckParams &params,
                                       Rng &rng) {
  switch (config.kind) {
  case CheckKind::AdditiveD20: {
    std::vector<std::string_view> views;
    views.reserve(config.bonusStats.size());
    for (const std::string &stat : config.bonusStats) {
      views.push_back(stat);
    }
    return resolveAdditiveD20(actor, target, config.diceExpression, views, config.targetStat,
                              params, rng);
  }
  case CheckKind::RollUnderD20:
    return resolveRollUnderD20(actor, config.attributes[0], config.useConfirmationRoll, params,
                               rng);
  case CheckKind::TripleRollUnderPool:
    return resolveTripleRollUnderPool(actor, config.attributes[0], config.attributes[1],
                                      config.attributes[2], config.poolStat, params, rng);
  case CheckKind::AttackVsDefense:
    return resolveAttackVsDefense(actor, target, config.attackStat, config.parryStat, params, rng);
  }
  return {};
}

} // namespace rpg_os
