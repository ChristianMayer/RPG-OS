// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file checks.hpp
 * @brief Shared check resolution algorithms.
 *
 * The check algorithms — additive d20, roll-under d20, the DSA (The Dark Eye)
 * 3d20 pool check, and attack-vs-defense — live here as templates over a
 * @c StatProvider and an @c RandomNumberGenerator. Both modes call the
 * very same functions:
 *   - generated specific-mode code calls them directly with string literals
 *     for every parameter, so the whole computation is inlined and dead
 *     branches fold away (zero dynamic allocation in the hot path);
 *   - the universal engine feeds them through @c CheckConfig (owning
 *     @c std::string) via the @c resolveCheck dispatcher.
 *
 * @par Why share the algorithms between modes at all?
 * A "check" is the heart of a tabletop ruleset, and the two modes must agree
 * on its outcome — the parity test exists precisely to prove that the
 * universal engine and the generated code roll the same dice, apply the same
 * criticals, and report the same margins. Writing the algorithms once, as
 * templates, is the only way to get that agreement without duplicating the
 * rules logic (and the risk of a copy drifting).
 *
 * @par Why templates with @c std::string_view parameters?
 * The universal mode passes owning @c std::string data (from the ruleset) and
 * the specific mode passes compile-time string literals. Templating on the
 * @c StatProvider and taking @c std::string_view for every parameter means the
 * same source serves both: for the specific mode the compiler can see the
 * exact stat ids at compile time and fold the whole check into a few
 * instructions.
 */
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
///
/// @par Why exactly these four?
/// These are the dice mechanisms the shipped rulesets actually use: D&D 5e
/// resolves attacks and ability checks as "1d20 + bonuses vs. a difficulty",
/// while The Dark Eye uses roll-under attributes, 3d20 talent checks, and an
/// attack-vs-defense contest. Keeping the set closed means the universal
/// dispatcher (a @c switch) and the code generator (which emits named methods)
/// both map onto a fixed vocabulary — a new game system with a fundamentally
/// new roll mechanism would extend this enum, but that is a rare, deliberate
/// change rather than a per-ruleset concern.
enum class CheckKind {
  AdditiveD20,         ///< 1d20 + bonuses >= threshold (D&D attack / ability check)
  RollUnderD20,        ///< 1d20 <= effective attribute (DSA attribute check)
  TripleRollUnderPool, ///< 3d20 vs three attributes, pool compensates (DSA talent)
  AttackVsDefense,     ///< attacker vs. defender roll-under contest (DSA combat)
};

/**
 * Describes HOW a check is resolved, as loaded from a ruleset's `check_types`.
 *
 * @par Why own its strings?
 * @c CheckConfig is built from ruleset JSON and must outlive the parse, so it
 * owns its @c std::string members. That makes it a self-contained value the
 * universal engine can hand to the dispatcher without any lifetime dependency
 * on the @ref Ruleset that produced it — and a stable thing for generated code
 * to mirror with `constexpr` configs.
 */
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
  /// A fixed-size array keeps the struct trivially copyable and lets the
  /// generated code emit it as a `constexpr` aggregate; @ref numAttributes
  /// records how many of the three slots are actually used.
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
///
/// @par Why separate per-call inputs from the config?
/// The config says @em how a check is resolved (which mechanism, which stats);
/// this struct carries the values that change @em per invocation — the
/// difficulty imposed by the situation, and situational modifiers such as
/// cover or a bonus die. Keeping them apart means the config can be built once
/// and reused across many checks with different difficulties.
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
///
/// @par Why a formula instead of a table?
/// The official rule is a per-3-points band. Expressing it as a closed-form
/// division (instead of a lookup table) keeps it in one line, matches the
/// source rule exactly, and is trivially reproducible in the generated code.
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
///
/// @par Why natural 20/1 override the comparison?
/// D&D-style games treat the die faces as dramatic special cases: a natural
/// 20 hits even against an unattainable AC, a natural 1 misses even a trivial
/// target. Encoding that here (rather than in the ruleset) keeps the rule in
/// the shared algorithm both modes use, so the parity guarantee covers it.
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
///
/// @par Why the eav < 1 early-out?
/// With a roll-under mechanic the effective attribute value (EAV) is the
/// highest number that can still succeed; if it drops below 1 no roll can
/// possibly pass, so the check fails without consuming an RNG draw — keeping
/// scripted-RNG tests exact and avoiding a pointless die roll.
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
///
/// @par Why model the pool as "remaining points"?
/// The leftover pool is the *primary* output of a real TDE talent check — it
/// feeds the quality level and lets the game rule on "how well" the check
/// went. Storing it in @ref CheckResult::remainingPool (and the derived
/// quality in @ref CheckResult::qualityLevel) means the caller never has to
/// re-derive success quality from the raw dice.
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
///
/// @par Why two staged rolls?
/// This mirrors the source rule: the parry only happens after a successful
/// attack, so the defense roll must not consume RNG (or affect the result)
/// when the attack already missed. Staging keeps the RNG stream aligned with
/// the rules — important for scripted-RNG tests and for cross-mode parity.
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

/// Runtime dispatcher over @ref CheckKind. This is the *universal-mode* entry
/// point: it turns a ruleset-supplied @ref CheckConfig into a call to one of
/// the algorithm templates. Specific mode calls the algorithm functions
/// directly with `constexpr` configs instead, so no runtime dispatch is paid
/// there.
///
/// @par Why a separate dispatcher at all?
/// Keeping the dispatch separate from the algorithms means the universal mode
/// gets a clean @c switch over the config while the generated mode can bypass
/// it entirely — the algorithms are the shared contract, the dispatcher is an
/// implementation detail of only one mode.
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
