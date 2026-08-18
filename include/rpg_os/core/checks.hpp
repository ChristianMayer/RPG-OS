// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file checks.hpp
 * @defgroup rpg_os_core Shared core — mechanics and value types
 * @brief Generic, data-driven check resolution.
 *
 * A "check" is the heart of a tabletop ruleset: the engine must roll some
 * dice and decide success, margins, and criticals. The engine deliberately
 * knows **no ruleset-specific mechanisms** — there is no "D&D roll" or
 * "The Dark Eye talent check" anywhere in this header. Instead a check is
 * described entirely by a @c CheckRecipe: which dice to roll, how to derive
 * the reference value the roll is compared against, which way to compare,
 * how to grade criticals, and how difficulty parameters are applied.
 *
 * The universal engine interprets the recipe at runtime (loaded from the
 * ruleset JSON), and the generated specific-mode code calls the very same
 * @c resolveCheck with a `constexpr` recipe, so the compiler folds the whole
 * check into a few instructions. Adding a new game system — including one
 * with a new combination of dice, comparison, and grading — requires only a
 * new JSON recipe, never a change to this (or any other universal) code.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <rpg_os/common/types.hpp>
#include <rpg_os/core/concepts.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// Which way a rolled value is compared against the reference.
enum class Comparison : int8_t {
  GreaterEqual, ///< the (bonus-adjusted) roll must be >= the reference
  LessEqual     ///< the roll must be <= the reference
};

/// Where the reference value of a check comes from.
enum class ThresholdSource : int8_t {
  Difficulty, ///< the value in @c CheckParams::difficulty
  ActorStat,  ///< one of the acting entity's stats
  TargetStat  ///< one of the target entity's stats
};

/// The fundamental shape of a check.
///
/// @par Why four resolutions?
/// These are the generic building blocks every ruleset's checks are built
/// from: a single roll against a reference (threshold), several independent
/// rolls that draw on a shared pool (pool), a two-sided contest (opposed),
/// and a single roll against a chance derived from two characteristics
/// (resistance). They are not named after any game; a ruleset composes them
/// with the other recipe fields to express exactly its own rules.
enum class Resolution : int8_t {
  Threshold, ///< roll against a single reference value
  Pool,      ///< independent rolls, overshoot drawn from a pool
  Opposed,   ///< attacker rolls vs. defender rolls
  Resistance ///< single roll against a table-derived chance
};

/// How critical successes / failures are detected.
enum class CriticalStyle : int8_t {
  None,          ///< no natural criticals
  Face,          ///< a specific die face (optionally confirmed by a re-roll)
  DoubleRoll,    ///< two equal dice among a set (e.g. double-1 / double-20)
  PercentileBand ///< graded percentile bands (see @ref successLevelFor)
};

/// How much information the check result carries.
enum class Grading : int8_t {
  None,       ///< plain pass / fail
  Percentile, ///< graded success levels (Critical / Special / Success / ...)
  PoolQuality ///< a quality level derived from the remaining pool
};

/// How @c CheckParams::difficulty is applied to the reference value.
enum class DifficultyMode : int8_t {
  ToThreshold, ///< difficulty *is* the reference (a fixed DC)
  ToStat       ///< difficulty is added to the stat that forms the reference
};

/// Whether a situational difficulty scale (Easy / Difficult) scales the
/// reference multiplicatively.
enum class DifficultyMultiplier : int8_t {
  None,       ///< no multiplicative difficulty
  DoubleHalve ///< Easy doubles the reference, Difficult halves it
};

/// A complete, self-contained description of how one check is resolved.
///
/// @par Why one struct with every field?
/// A recipe is the union of everything a ruleset may declare for a check; only
/// the fields relevant to the chosen @ref Resolution are read, so rulesets
/// never write more than they need. The struct owns its strings so a recipe
/// parsed from JSON outlives the parse, and generated code builds the
/// equivalent recipe from the @c "1d20"_dice literals — the dice are parsed
/// once when the recipe is constructed, never on each roll. This is the one
/// piece of "ruleset specific" wiring that lives in the *generated* header,
/// never in the core.
struct CheckRecipe {
  /// The fundamental shape (see @ref Resolution).
  Resolution resolution{Resolution::Threshold};
  /// The dice expression rolled by the check (e.g. "1d20", "1d100", "3d20").
  /// Held as a parsed @c DiceExpression so the dice are interpreted once when
  /// the recipe is built, not on every resolution.
  DiceExpression dice{"1d20"};

  /// Threshold resolution: the comparison direction.
  Comparison comparison{Comparison::GreaterEqual};
  /// Threshold resolution: where the reference value comes from.
  ThresholdSource thresholdSource{ThresholdSource::Difficulty};
  /// Threshold resolution: the stat used when @ref thresholdSource is
  /// @c ActorStat or @c TargetStat.
  std::string thresholdStat{};
  /// Threshold resolution: actor stats added to the rolled total (additive
  /// checks, e.g. D&D's attribute bonuses on top of the d20).
  std::vector<std::string> bonusStats{};

  /// Pool resolution: the attributes each independent roll is checked against.
  /// @ref numPoolAttributes records how many of the three slots are used.
  std::array<std::string, 3> poolAttributes{};
  std::size_t numPoolAttributes{0};
  /// Pool resolution: the stat that provides the pool (e.g. a skill rating).
  std::string poolStat{};

  /// Opposed / resistance resolution: the attacker's (active) stat.
  std::string attackStat{};
  /// Opposed / resistance resolution: the defender's (passive) stat.
  std::string parryStat{};
  /// Opposed resolution: true compares graded success levels (a hit needs a
  /// strictly higher level); false resolves a staged roll-under contest
  /// (parry only after a successful attack).
  bool compareLevels{false};

  /// Critical success detection (see @ref CriticalStyle).
  CriticalStyle criticalStyle{CriticalStyle::None};
  /// Critical success: the die face that is a critical (@c Face).
  int criticalFace{0};
  /// Critical success: re-roll to confirm (@c Face).
  bool criticalConfirm{false};
  /// Critical failure detection (see @ref CriticalStyle).
  CriticalStyle fumbleStyle{CriticalStyle::None};
  /// Critical failure: the die face that is a fumble (@c Face).
  int fumbleFace{0};
  /// Critical failure: re-roll to confirm (@c Face).
  bool fumbleConfirm{false};

  /// How much information the result carries (see @ref Grading).
  Grading grading{Grading::None};

  /// How @c CheckParams::difficulty is applied (see @ref DifficultyMode).
  DifficultyMode difficultyMode{DifficultyMode::ToThreshold};
  /// Whether Easy / Difficult scales the reference multiplicatively.
  DifficultyMultiplier difficultyMultiplier{DifficultyMultiplier::None};
};

/// Situational difficulty scale for a percentile roll-under check.
///
/// @par Why a scale rather than a flat modifier?
/// Some percentile systems grade difficulty by multiplying the skill rating —
/// Easy doubles it, Difficult halves it — rather than adding a constant.
/// @c CheckParams::difficulty still carries the flat situational modifiers;
/// the scale is orthogonal to them.
enum class DifficultyScale : int8_t {
  Average = 0,   ///< no change to the reference
  Easy = 1,      ///< double the effective reference
  Difficult = -1 ///< halve the effective reference
};

/// Whether a check is rolled with advantage or disadvantage (roll twice, keep
/// the better / worse total).
///
/// @par Why a per-call parameter rather than a recipe field?
/// Advantage is a *situational* quality: it comes from active conditions (a
/// blinded attacker attacks with disadvantage; attacks against a blinded
/// defender gain advantage) or from a caller's ruling, never from the shape of
/// the check itself. Keeping it in @c CheckParams means one recipe serves both
/// the plain and the advantaged forms of the same check. When both advantage
/// and disadvantage are present they cancel out and the check rolls once.
enum class AdvantageMode : int8_t {
  None,        ///< roll once (no advantage)
  Advantage,   ///< roll twice, keep the higher total
  Disadvantage ///< roll twice, keep the lower total
};

/// Per-call inputs shared by both modes.
///
/// @par Why separate per-call inputs from the recipe?
/// The recipe says @em how a check is resolved (which dice, which stats);
/// this struct carries the values that change @em per invocation — the
/// difficulty imposed by the situation, and situational modifiers. Keeping
/// them apart means the recipe can be built once and reused across many
/// checks with different difficulties.
struct CheckParams {
  /// Threshold resolution: the reference when @c ThresholdSource::Difficulty;
  /// otherwise the amount added to the stat that forms the reference.
  int32_t difficulty{0};
  /// Extra flat modifier added to the rolled total (additive checks) or to
  /// the pool (pool checks).
  int32_t situationalModifier{0};
  /// Multiplicative difficulty scale (Easy doubles / Difficult halves the
  /// reference). Only checks that opt in via @c CheckRecipe::difficultyMultiplier
  /// honour it; see @ref DifficultyScale.
  DifficultyScale difficultyScale{DifficultyScale::Average};
  /// Advantage / disadvantage for this roll (see @ref AdvantageMode).
  AdvantageMode advantage{AdvantageMode::None};
  /// Bonus dice rolled in addition to the check's dice and added to the
  /// additive total (a D&D Bless die, a bane penalty die, ...). Aggregated by
  /// the engine from active conditions / effects / traits; empty means none.
  std::vector<DiceExpression> bonusDice;
  /// Whether the check is automatically a failure (e.g. "you automatically
  /// fail Strength and Dexterity saving throws"). When true no dice are
  /// rolled and the result is a failure.
  bool autoFail{false};
};

/// The Dark Eye quality level derived from leftover skill points:
/// 0-3 -> 1, 4-6 -> 2, 7-9 -> 3, 10-12 -> 4, 13-15 -> 5, 16+ -> 6.
///
/// @par Why a formula instead of a table?
/// The official rule is a per-3-points band. Expressing it as a closed-form
/// division (instead of a lookup table) keeps it in one line, matches the
/// source rule exactly, and is trivially reproducible in the generated code.
[[nodiscard]] constexpr int32_t qualityLevelFromRemaining(int32_t remaining) noexcept {
  if (remaining <= 0) {
    return 1;
  }
  return (remaining - 1) / 3 + 1;
}

/// Maps a percentile roll to a graded success level against `stat`.
///
/// @par Why a single helper shared by solo and opposed rolls?
/// Any check that grades percentiles — a lone roll-under check or an opposed
/// contest — grades the same way: Critical at stat/20, Special at stat/5,
/// Success at stat, and a skill-dependent fumble band — and opposed checks
/// must compare the two sides' levels. One helper guarantees every use can
/// never disagree about what a roll means.
///
/// The thresholds follow the classic Skill Results Table: the critical range
/// is ceil(stat/20), the special range ceil(stat/5), and the fumble band
/// starts at 95 + ceil(stat/20), capped at 100 so a roll of 00 always fumbles.
[[nodiscard]] constexpr SuccessLevel successLevelFor(int32_t stat, int roll) noexcept {
  const int rawFumbleMin = 95 + (stat + 19) / 20; // 95 + ceil(stat/20)
  const int fumbleMin = rawFumbleMin < 100 ? rawFumbleMin : 100;
  if (roll >= fumbleMin) {
    return SuccessLevel::Fumble;
  }
  if (roll <= (stat + 19) / 20) { // ceil(stat/20)
    return SuccessLevel::Critical;
  }
  if (roll <= (stat + 4) / 5) { // ceil(stat/5)
    return SuccessLevel::Special;
  }
  return roll <= stat ? SuccessLevel::Success : SuccessLevel::Failure;
}

namespace detail {

/// Rolls a single die of `sides` (a shared primitive used by every
/// resolution; the recipe decides how many times it is called).
template <RandomNumberGenerator Rng> constexpr int rollDie(int sides, Rng &rng) {
  return rng(1, sides);
}

/// Applies a multiplicative difficulty scale to a reference value.
[[nodiscard]] constexpr int32_t scaleByDifficulty(int32_t value, DifficultyScale scale) noexcept {
  if (scale == DifficultyScale::Easy) {
    return value * 2;
  }
  if (scale == DifficultyScale::Difficult) {
    return value / 2;
  }
  return value;
}

/// Derives the reference value a threshold check is rolled against, applying
/// the recipe's difficulty mode and multiplicative scale.
template <StatProvider Actor, StatProvider Target>
[[nodiscard]] constexpr int32_t thresholdReference(const Actor &actor, const Target &target,
                                                   const CheckRecipe &recipe,
                                                   const CheckParams &params) {
  int32_t reference = 0;
  if (recipe.thresholdSource == ThresholdSource::TargetStat) {
    reference = static_cast<int32_t>(target.getStat(recipe.thresholdStat));
  } else if (recipe.thresholdSource == ThresholdSource::ActorStat) {
    reference = static_cast<int32_t>(actor.getStat(recipe.thresholdStat));
  }
  if (recipe.difficultyMode == DifficultyMode::ToStat) {
    reference += params.difficulty;
  } else if (recipe.difficultyMode == DifficultyMode::ToThreshold &&
             recipe.thresholdSource == ThresholdSource::Difficulty) {
    reference = params.difficulty;
  }
  if (recipe.difficultyMultiplier == DifficultyMultiplier::DoubleHalve) {
    reference = scaleByDifficulty(reference, params.difficultyScale);
  }
  return reference;
}

/// Rolls a dice expression once, returning the recorded faces and their sum.
template <RandomNumberGenerator Rng>
[[nodiscard]] constexpr std::pair<std::vector<int>, int> rollWithTotal(const DiceExpression &dice,
                                                                       Rng &rng) {
  std::vector<int> rolls = dice.roll(rng);
  int total = 0;
  for (const int die : rolls) {
    total += die;
  }
  return {std::move(rolls), total};
}

/// Rolls the expression a second time and keeps the better (advantage) or
/// worse (disadvantage) total, replacing the recorded rolls.
template <RandomNumberGenerator Rng>
constexpr void applyAdvantage(std::vector<int> &rawDiceRolls, int &diceTotal,
                              const DiceExpression &dice, AdvantageMode advantage, Rng &rng) {
  auto [second, secondTotal] = rollWithTotal(dice, rng);
  const bool keepSecond =
      advantage == AdvantageMode::Advantage ? secondTotal > diceTotal : secondTotal < diceTotal;
  if (keepSecond) {
    rawDiceRolls = std::move(second);
    diceTotal = secondTotal;
  }
}

/// Sums the actor's bonus stats, bonus dice, and the situational modifier into
/// an additive total, recording any bonus dice faces in `rawDiceRolls`.
template <StatProvider Actor, RandomNumberGenerator Rng>
[[nodiscard]] constexpr int additiveBonuses(const Actor &actor, const CheckRecipe &recipe,
                                            const CheckParams &params,
                                            std::vector<int> &rawDiceRolls, Rng &rng) {
  int total = 0;
  for (const std::string &stat : recipe.bonusStats) {
    total += static_cast<int32_t>(actor.getStat(stat));
  }
  for (const DiceExpression &bonus : params.bonusDice) {
    auto [rolls, bonusTotal] = rollWithTotal(bonus, rng);
    rawDiceRolls.insert(rawDiceRolls.end(), rolls.begin(), rolls.end());
    total += bonusTotal;
  }
  total += params.situationalModifier;
  return total;
}

/// Grades a threshold check's outcome: criticals, fumbles, percentile success
/// levels, or a plain pass / fail.
template <RandomNumberGenerator Rng>
constexpr void gradeThresholdResult(CheckResult &result, int roll, int total, int32_t reference,
                                    const CheckRecipe &recipe, Rng &rng) {
  if (recipe.criticalStyle == CriticalStyle::Face && roll == recipe.criticalFace) {
    result.isSuccess = true;
    if (recipe.criticalConfirm) {
      const int confirm = recipe.dice.rollSum(rng);
      result.rawDiceRolls.push_back(confirm);
      result.isCriticalSuccess = confirm <= reference;
    } else {
      result.isCriticalSuccess = true;
    }
  } else if (recipe.fumbleStyle == CriticalStyle::Face && roll == recipe.fumbleFace) {
    result.isSuccess = false;
    if (recipe.fumbleConfirm) {
      const int confirm = recipe.dice.rollSum(rng);
      result.rawDiceRolls.push_back(confirm);
      result.isCriticalFailure = confirm > reference || confirm == recipe.fumbleFace;
    } else {
      result.isCriticalFailure = true;
    }
  } else if (recipe.grading == Grading::Percentile) {
    result.successLevel = successLevelFor(reference, roll);
    result.isCriticalSuccess = result.successLevel == SuccessLevel::Critical;
    result.isCriticalFailure = result.successLevel == SuccessLevel::Fumble;
    result.isSuccess = result.successLevel >= SuccessLevel::Success;
  } else {
    result.isSuccess =
        recipe.comparison == Comparison::GreaterEqual ? total >= reference : roll <= reference;
  }
}

} // namespace detail

/// Threshold resolution: roll the recipe's dice and compare the result
/// (optionally plus actor bonus stats) against a single reference value.
///
/// @par Why one function for both additive and roll-under checks?
/// They are the same shape — a single roll against a reference — differing
/// only in the comparison direction, the source of the reference, and how
/// difficulty is applied. Encoding those as recipe fields (rather than as
/// separate named algorithms) is what lets the universal engine handle any
/// new system's solo checks without new code.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] constexpr CheckResult resolveThresholdCheck(const Actor &actor, const Target &target,
                                                          const CheckRecipe &recipe,
                                                          const CheckParams &params, Rng &rng) {
  CheckResult result;
  const DiceExpression &dice = recipe.dice;

  // Derive the reference value the roll is compared against.
  const int32_t reference = detail::thresholdReference(actor, target, recipe, params);
  // Roll-under checks fail outright when the reference is below the minimum
  // rollable value (1): no die can possibly meet it, so no RNG is consumed.
  if (recipe.comparison == Comparison::LessEqual && reference < 1) {
    result.isSuccess = false;
    return result;
  }

  auto [rolls, diceTotal] = detail::rollWithTotal(dice, rng);
  result.rawDiceRolls = std::move(rolls);

  // Advantage / disadvantage: roll the whole expression a second time and
  // keep the better (advantage) or worse (disadvantage) total, replacing the
  // recorded dice so critical detection and callers see the kept roll.
  if (params.advantage != AdvantageMode::None) {
    detail::applyAdvantage(result.rawDiceRolls, diceTotal, dice, params.advantage, rng);
  }

  // Additive checks sum bonus stats, bonus dice, and the situational modifier
  // into the rolled total; roll-under checks compare the bare roll.
  const int roll = diceTotal;
  const int total =
      recipe.comparison == Comparison::GreaterEqual
          ? diceTotal + detail::additiveBonuses(actor, recipe, params, result.rawDiceRolls, rng)
          : diceTotal;

  result.marginOfSuccess = static_cast<int32_t>(
      recipe.comparison == Comparison::GreaterEqual ? total - reference : reference - roll);

  detail::gradeThresholdResult(result, roll, total, reference, recipe, rng);
  return result;
}

/// Pool resolution: each independent roll is checked against its own effective
/// attribute value; every overshoot (the amount by which a roll exceeds its
/// attribute) is drawn from a shared pool. The check succeeds while the pool
/// does not go negative.
///
/// @par Why model the pool as "remaining points"?
/// The leftover pool is the *primary* output of such a check — it feeds the
/// quality level and lets the game rule on "how well" the check went. Storing
/// it in @c CheckResult::remainingPool (and the derived quality in
/// @c CheckResult::qualityLevel) means the caller never has to re-derive
/// success quality from the raw dice.
template <StatProvider Actor, RandomNumberGenerator Rng>
[[nodiscard]] constexpr CheckResult resolvePoolCheck(const Actor &actor, const CheckRecipe &recipe,
                                                     const CheckParams &params, Rng &rng) {
  CheckResult result;
  const DiceExpression &dice = recipe.dice;

  // Every effective attribute must be at least 1 before any die is rolled:
  // otherwise that part of the check can never pass, and no RNG is consumed.
  const std::size_t n = recipe.numPoolAttributes;
  std::array<int32_t, 3> eav{};
  for (std::size_t i = 0; i < n; ++i) {
    const int32_t effective =
        static_cast<int32_t>(actor.getStat(recipe.poolAttributes.at(i))) + params.difficulty;
    eav.at(i) = effective;
    if (effective < 1) {
      result.isSuccess = false;
      return result;
    }
  }

  result.rawDiceRolls = dice.roll(rng);
  int32_t totalOvershoot = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const int32_t roll = result.rawDiceRolls[i];
    const int32_t effective = eav.at(i);
    totalOvershoot += roll > effective ? roll - effective : 0;
  }

  const int32_t pool =
      static_cast<int32_t>(actor.getStat(recipe.poolStat)) + params.situationalModifier;
  const int32_t finalPool = pool - totalOvershoot;
  result.remainingPool = finalPool;
  result.marginOfSuccess = finalPool;

  bool doubleLow = false;
  bool doubleHigh = false;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      doubleLow = doubleLow || (result.rawDiceRolls[i] == 1 && result.rawDiceRolls[j] == 1);
      doubleHigh = doubleHigh || (result.rawDiceRolls[i] == 20 && result.rawDiceRolls[j] == 20);
    }
  }
  result.isCriticalSuccess = doubleLow;
  result.isCriticalFailure = doubleHigh;
  result.isSuccess = finalPool >= 0;
  if (doubleLow) {
    result.isSuccess = true;
  } else if (doubleHigh) {
    result.isSuccess = false;
  }
  if (recipe.grading == Grading::PoolQuality) {
    result.qualityLevel = result.isSuccess ? qualityLevelFromRemaining(finalPool) : 0;
  }
  return result;
}

/// Opposed resolution: the attacker rolls against their attack stat and the
/// defender against their parry stat. With `compareLevels` the two rolls are
/// graded and the attack strikes only on a strictly higher success level;
/// otherwise the parry is staged — it only happens after a successful attack.
///
/// @par Why two distinct contest models?
/// A contest can either compare the two raw roll-under results (attacker hits
/// on a success the defender fails, parry staged) or compare graded success
/// levels (a Special beats a Success, equal levels mean the defender wins).
/// Both are legitimate, and both appear in shipped systems; encoding the
/// choice as a recipe field keeps one generic resolver for both.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] constexpr CheckResult resolveOpposedCheck(const Actor &actor, const Target &target,
                                                        const CheckRecipe &recipe,
                                                        const CheckParams &params, Rng &rng) {
  CheckResult result;
  const DiceExpression &dice = recipe.dice;
  const int32_t attackValue =
      static_cast<int32_t>(actor.getStat(recipe.attackStat)) + params.difficulty;
  const int32_t parryValue = static_cast<int32_t>(target.getStat(recipe.parryStat));

  if (recipe.compareLevels) {
    // Both rolls are needed up front: the two success levels are compared
    // directly, so the defender's roll must exist even when the attack is a
    // critical. Rolling both keeps the RNG stream aligned with the rules.
    const int attackRoll = dice.rollSum(rng);
    const int parryRoll = dice.rollSum(rng);
    result.rawDiceRolls = {attackRoll, parryRoll};
    result.marginOfSuccess = static_cast<int32_t>(attackValue - attackRoll);
    const SuccessLevel attackLevel = successLevelFor(attackValue, attackRoll);
    const SuccessLevel parryLevel = successLevelFor(parryValue, parryRoll);
    result.successLevel = attackLevel;
    result.isCriticalSuccess = attackLevel == SuccessLevel::Critical;
    result.isCriticalFailure = attackLevel == SuccessLevel::Fumble;
    // A hit needs a strictly higher success level (and at least a plain
    // success); equal levels mean the defender parries.
    result.isSuccess = attackLevel >= SuccessLevel::Success && attackLevel > parryLevel;
    return result;
  }

  // Staged roll-under contest: the parry only happens after a successful
  // attack, so the defense roll must not consume RNG (or affect the result)
  // when the attack already missed.
  const int attackRoll = dice.rollSum(rng);
  result.rawDiceRolls.push_back(attackRoll);
  result.isCriticalSuccess = attackRoll == recipe.criticalFace;
  result.isCriticalFailure = attackRoll == recipe.fumbleFace;
  const bool attackHits =
      attackRoll == recipe.criticalFace
          ? true
          : (attackRoll == recipe.fumbleFace ? false : attackRoll <= attackValue);
  result.marginOfSuccess = static_cast<int32_t>(attackValue - attackRoll);
  if (!attackHits) {
    result.isSuccess = false;
    return result;
  }
  const int parryRoll = dice.rollSum(rng);
  result.rawDiceRolls.push_back(parryRoll);
  const bool parrySucceeds =
      parryRoll == recipe.criticalFace
          ? true
          : (parryRoll == recipe.fumbleFace ? false : parryRoll <= parryValue);
  result.isSuccess = !parrySucceeds;
  return result;
}

/// Resistance resolution: a single roll against the chance derived from two
/// characteristics (chance = 50 + 5 * (active - passive)), with automatic
/// success / failure beyond the rollable band.
///
/// @par Why a single roll against a computed chance (not an opposed contest)?
/// A resistance table yields one percentage and the active side rolls it; only
/// the acting character consumes RNG, matching the table exactly.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] constexpr CheckResult resolveResistanceCheck(const Actor &actor, const Target &target,
                                                           const CheckRecipe &recipe,
                                                           const CheckParams &params, Rng &rng) {
  CheckResult result;
  const int32_t active = static_cast<int32_t>(actor.getStat(recipe.attackStat)) + params.difficulty;
  const int32_t passive = static_cast<int32_t>(target.getStat(recipe.parryStat));
  const int32_t chance = 50 + 5 * (active - passive);
  if (chance >= 100) {
    result.isSuccess = true; // automatic success (off the top of the table)
    result.successLevel = SuccessLevel::Success;
    return result;
  }
  if (chance <= 0) {
    result.isSuccess = false; // automatic failure (off the bottom of the table)
    result.successLevel = SuccessLevel::Failure;
    return result;
  }
  const DiceExpression &dice = recipe.dice;
  const int roll = dice.rollSum(rng);
  result.rawDiceRolls.push_back(roll);
  result.marginOfSuccess = static_cast<int32_t>(chance - roll);
  result.isSuccess = roll <= chance;
  result.successLevel = result.isSuccess ? SuccessLevel::Success : SuccessLevel::Failure;
  return result;
}

/// The single generic check resolver. Dispatches on the (generic)
/// @ref Resolution of a @ref CheckRecipe and applies every other recipe field
/// data-driven — the universal engine feeds it a runtime recipe from the
/// ruleset, generated code feeds it a `constexpr` recipe so the whole check
/// folds at compile time. There is deliberately no per-ruleset branch here:
/// a new game system is a new JSON recipe, nothing else.
///
/// @par Why one dispatcher and no ruleset-specific algorithms?
/// All check mechanics in this library are configurations of the four generic
/// resolutions above. A single resolver keeps the universal and specific
/// modes on the exact same code path (the parity guarantee), and guarantees a
/// new ruleset can never need a change to this file.
template <StatProvider Actor, StatProvider Target, RandomNumberGenerator Rng>
[[nodiscard]] constexpr CheckResult resolveCheck(const Actor &actor, const Target &target,
                                                 const CheckRecipe &recipe,
                                                 const CheckParams &params, Rng &rng) {
  // An automatic failure ("you automatically fail Strength and Dexterity
  // saving throws") fails the check outright without consuming any RNG.
  if (params.autoFail) {
    CheckResult failed;
    failed.isSuccess = false;
    return failed;
  }
  switch (recipe.resolution) {
  case Resolution::Threshold:
    return resolveThresholdCheck(actor, target, recipe, params, rng);
  case Resolution::Pool:
    return resolvePoolCheck(actor, recipe, params, rng);
  case Resolution::Opposed:
    return resolveOpposedCheck(actor, target, recipe, params, rng);
  case Resolution::Resistance:
    return resolveResistanceCheck(actor, target, recipe, params, rng);
  }
  return {};
}

} // namespace rpg_os
