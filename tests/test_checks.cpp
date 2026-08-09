// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Tests for the shared check resolution algorithms (rpg_os::checks).
//
// Includes the acceptance case studies from the design spec (content.md §9):
//   - D&D attack roll vs. AC (additive d20)
//   - DSA 3d20 talent check with Talent Prowess pool
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/core/checks.hpp>

using rpg_os::CheckConfig;
using rpg_os::CheckKind;
using rpg_os::CheckParams;
using rpg_os::CheckResult;

namespace {

CheckConfig additiveConfig() {
  CheckConfig cfg;
  cfg.kind = CheckKind::AdditiveD20;
  cfg.diceExpression = "1d20";
  cfg.bonusStats = {"STR_mod", "proficiency_bonus"};
  cfg.targetStat = "AC";
  return cfg;
}

CheckConfig dsaTalentConfig() {
  CheckConfig cfg;
  cfg.kind = CheckKind::TripleRollUnderPool;
  cfg.attributes = {"COU", "AGI", "STR"};
  cfg.numAttributes = 3;
  cfg.poolStat = "climbing";
  return cfg;
}

CheckConfig attributeCheckConfig(bool confirmation) {
  CheckConfig cfg;
  cfg.kind = CheckKind::RollUnderD20;
  cfg.attributes[0] = "COU";
  cfg.numAttributes = 1;
  cfg.useConfirmationRoll = confirmation;
  return cfg;
}

CheckConfig combatConfig() {
  CheckConfig cfg;
  cfg.kind = CheckKind::AttackVsDefense;
  cfg.attackStat = "AT";
  cfg.parryStat = "PA";
  return cfg;
}

} // namespace

// ---------------------------------------------------------------------------
// Additive d20 (D&D)
// ---------------------------------------------------------------------------

TEST_CASE("AdditiveD20: hit when total meets the target's AC") {
  MockStats fighter;
  fighter.values = {{"STR_mod", 3}, {"proficiency_bonus", 2}};
  MockStats orc;
  orc.values = {{"AC", 15}};
  const CheckConfig cfg = additiveConfig();
  const CheckParams params;

  auto r1 = script({10}); // 10 + 3 + 2 = 15 >= 15 -> hit, margin 0
  const CheckResult hit = rpg_os::resolveCheck(fighter, orc, cfg, params, r1);
  CHECK(hit.isSuccess);
  CHECK(hit.marginOfSuccess == 0);
  CHECK(hit.rawDiceRolls == std::vector<int>{10});

  auto r2 = script({8}); // 8 + 3 + 2 = 13 < 15 -> miss, margin -2
  const CheckResult miss = rpg_os::resolveCheck(fighter, orc, cfg, params, r2);
  CHECK_FALSE(miss.isSuccess);
  CHECK(miss.marginOfSuccess == -2);

  auto r3 = script({11}); // 11 + 3 + 2 = 16 -> hit, margin 1
  const CheckResult good = rpg_os::resolveCheck(fighter, orc, cfg, params, r3);
  CHECK(good.isSuccess);
  CHECK(good.marginOfSuccess == 1);
}

TEST_CASE("AdditiveD20: natural 20 always hits, natural 1 always misses") {
  MockStats fighter;
  fighter.values = {{"STR_mod", 0}, {"proficiency_bonus", 0}};
  MockStats target;
  target.values = {{"AC", 30}}; // unreachable by modifiers alone
  const CheckConfig cfg = additiveConfig();
  const CheckParams params;

  auto r1 = script({20});
  const CheckResult crit = rpg_os::resolveCheck(fighter, target, cfg, params, r1);
  CHECK(crit.isSuccess);
  CHECK(crit.isCriticalSuccess);

  auto r2 = script({1});
  const CheckResult fumble = rpg_os::resolveCheck(fighter, target, cfg, params, r2);
  CHECK_FALSE(fumble.isSuccess);
  CHECK(fumble.isCriticalFailure);
}

TEST_CASE("AdditiveD20: uses params.difficulty as DC when no target stat") {
  MockStats rogue;
  rogue.values = {{"DEX_mod", 4}, {"proficiency_bonus", 3}};
  CheckConfig cfg = additiveConfig();
  cfg.targetStat = "";
  cfg.bonusStats = {"DEX_mod", "proficiency_bonus"};
  CheckParams params;
  params.difficulty = 15;

  auto r1 = script({8}); // 8 + 4 + 3 = 15 >= 15 -> success
  const CheckResult ok = rpg_os::resolveCheck(rogue, rpg_os::NullStatProvider{}, cfg, params, r1);
  CHECK(ok.isSuccess);

  auto r2 = script({7}); // 7 + 4 + 3 = 14 < 15 -> failure
  const CheckResult bad = rpg_os::resolveCheck(rogue, rpg_os::NullStatProvider{}, cfg, params, r2);
  CHECK_FALSE(bad.isSuccess);
}

// ---------------------------------------------------------------------------
// TripleRollUnderPool (DSA skill check) — real TDE 2nd-printing rules
// ---------------------------------------------------------------------------

TEST_CASE("TripleRollUnderPool: unmodified skill check succeeds with SP left") {
  // Geron: COU 12, AGI 13, STR 11, Climbing skill rating 7, no modifier.
  MockStats geron;
  geron.values = {{"COU", 12}, {"AGI", 13}, {"STR", 11}, {"climbing", 7}};
  const CheckConfig cfg = dsaTalentConfig();
  const CheckParams params;

  // Rolls 14, 12, 11 vs EAVs 12, 13, 11: die1 over by 2, die2/3 fine. The
  // overshoot (2) is paid from the 7 skill points -> 5 left. Success, QL 2.
  auto rng = script({14, 12, 11});
  const CheckResult result =
      rpg_os::resolveCheck(geron, rpg_os::NullStatProvider{}, cfg, params, rng);
  CHECK(result.isSuccess);
  CHECK(result.remainingPool == 5);
  CHECK(result.qualityLevel == 2);
  CHECK(result.rawDiceRolls == std::vector<int>{14, 12, 11});
}

TEST_CASE("TripleRollUnderPool: difficulty modifies the EAVs (TDE)") {
  MockStats geron;
  geron.values = {{"COU", 12}, {"AGI", 13}, {"STR", 11}, {"climbing", 7}};
  const CheckConfig cfg = dsaTalentConfig();

  // Penalty -3 -> EAVs 9, 10, 8; overshoots 5 + 2 + 3 = 10 > 7 -> fail, -3 left.
  CheckParams penalty;
  penalty.difficulty = -3;
  auto rp = script({14, 12, 11});
  const CheckResult fail =
      rpg_os::resolveCheck(geron, rpg_os::NullStatProvider{}, cfg, penalty, rp);
  CHECK_FALSE(fail.isSuccess);
  CHECK(fail.remainingPool == -3);

  // Bonus +3 -> EAVs 15, 16, 14; all rolls pass -> 7 SP left, success, QL 3.
  CheckParams bonus;
  bonus.difficulty = 3;
  auto rb = script({14, 12, 11});
  const CheckResult ok = rpg_os::resolveCheck(geron, rpg_os::NullStatProvider{}, cfg, bonus, rb);
  CHECK(ok.isSuccess);
  CHECK(ok.remainingPool == 7);
  CHECK(ok.qualityLevel == 3);
}

TEST_CASE("TripleRollUnderPool: EAV below 1 fails automatically") {
  MockStats hero;
  hero.values = {{"COU", 5}, {"AGI", 12}, {"STR", 12}, {"climbing", 7}};
  const CheckConfig cfg = dsaTalentConfig();
  CheckParams penalty;
  penalty.difficulty = -6; // COU EAV = -1
  auto rng = script({});
  const CheckResult result =
      rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, penalty, rng);
  CHECK_FALSE(result.isSuccess);
  CHECK(result.rawDiceRolls.empty()); // no dice rolled
}

TEST_CASE("TripleRollUnderPool: double-1 is a critical success, double-20 a critical failure") {
  MockStats hero;
  hero.values = {{"COU", 8}, {"AGI", 8}, {"STR", 8}, {"climbing", 2}};
  const CheckConfig cfg = dsaTalentConfig();
  const CheckParams params;

  auto r1 = script({1, 1, 18}); // double-1 overrides the failed third roll
  const CheckResult crit = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r1);
  CHECK(crit.isSuccess);
  CHECK(crit.isCriticalSuccess);

  auto r2 = script({20, 20, 3}); // double-20 is an automatic failure
  const CheckResult botch = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r2);
  CHECK_FALSE(botch.isSuccess);
  CHECK(botch.isCriticalFailure);
}

// ---------------------------------------------------------------------------
// RollUnderD20 (DSA attribute check)
// ---------------------------------------------------------------------------

TEST_CASE("RollUnderD20: succeeds on a roll at or below the attribute") {
  MockStats hero;
  hero.values = {{"COU", 12}};
  const CheckConfig cfg = attributeCheckConfig(false);
  const CheckParams params;

  auto r1 = script({12});
  CHECK(rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r1).isSuccess);
  auto r2 = script({13});
  CHECK_FALSE(rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r2).isSuccess);
}

TEST_CASE("RollUnderD20: difficulty modifies the effective attribute") {
  MockStats hero;
  hero.values = {{"COU", 12}};
  const CheckConfig cfg = attributeCheckConfig(false);
  CheckParams bonus;
  bonus.difficulty = 3; // +3 -> EAV 15
  auto r1 = script({15});
  CHECK(rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, bonus, r1).isSuccess);
  auto r2 = script({16});
  CHECK_FALSE(rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, bonus, r2).isSuccess);

  CheckParams penalty;
  penalty.difficulty = -5; // -5 -> EAV 7
  auto r3 = script({8});
  CHECK_FALSE(rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, penalty, r3).isSuccess);
}

TEST_CASE("RollUnderD20: EAV below 1 is an automatic failure") {
  MockStats hero;
  hero.values = {{"COU", 5}};
  const CheckConfig cfg = attributeCheckConfig(false);
  CheckParams penalty;
  penalty.difficulty = -6; // EAV -1
  auto rng = script({});
  const CheckResult result =
      rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, penalty, rng);
  CHECK_FALSE(result.isSuccess);
  CHECK(result.rawDiceRolls.empty()); // no dice rolled
}

TEST_CASE("RollUnderD20: confirmation rolls decide criticals and botches") {
  MockStats hero;
  hero.values = {{"COU", 12}};
  const CheckConfig cfg = attributeCheckConfig(true);
  const CheckParams params;

  // nat 1, confirm 5 (<= 12): critical success.
  auto r1 = script({1, 5});
  const CheckResult crit = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r1);
  CHECK(crit.isSuccess);
  CHECK(crit.isCriticalSuccess);

  // nat 1, confirm 20 (> 12): regular success, not critical.
  auto r2 = script({1, 20});
  const CheckResult plain = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r2);
  CHECK(plain.isSuccess);
  CHECK_FALSE(plain.isCriticalSuccess);

  // nat 20, confirm 5 (<= 12): regular failure, not a botch.
  auto r3 = script({20, 5});
  const CheckResult fail = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r3);
  CHECK_FALSE(fail.isSuccess);
  CHECK_FALSE(fail.isCriticalFailure);

  // nat 20, confirm 20: botch.
  auto r4 = script({20, 20});
  const CheckResult botch = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r4);
  CHECK_FALSE(botch.isSuccess);
  CHECK(botch.isCriticalFailure);
}

TEST_CASE("RollUnderD20: without confirmation, nat 1 / nat 20 are direct") {
  MockStats hero;
  hero.values = {{"COU", 12}};
  const CheckConfig cfg = attributeCheckConfig(false);
  const CheckParams params;

  auto r1 = script({1});
  const CheckResult crit = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r1);
  CHECK(crit.isCriticalSuccess);

  auto r2 = script({20});
  const CheckResult botch = rpg_os::resolveCheck(hero, rpg_os::NullStatProvider{}, cfg, params, r2);
  CHECK(botch.isCriticalFailure);
}

// ---------------------------------------------------------------------------
// AttackVsDefense (DSA combat)
// ---------------------------------------------------------------------------

TEST_CASE("AttackVsDefense: hit when the attack succeeds and the parry fails") {
  MockStats attacker;
  attacker.values = {{"AT", 13}};
  MockStats defender;
  defender.values = {{"PA", 10}};
  const CheckConfig cfg = combatConfig();
  const CheckParams params;

  // attack 11 (<= 13) hits; parry 12 (> 10) fails -> hit.
  auto r1 = script({11, 12});
  CHECK(rpg_os::resolveCheck(attacker, defender, cfg, params, r1).isSuccess);

  // attack 11 hits; parry 5 (<= 10) succeeds -> parried.
  auto r2 = script({11, 5});
  CHECK_FALSE(rpg_os::resolveCheck(attacker, defender, cfg, params, r2).isSuccess);

  // attack 15 (> 13) misses; no parry roll needed.
  auto r3 = script({15});
  CHECK_FALSE(rpg_os::resolveCheck(attacker, defender, cfg, params, r3).isSuccess);
}

TEST_CASE("AttackVsDefense: natural 1 is a critical hit, natural 20 a fumble") {
  MockStats attacker;
  attacker.values = {{"AT", 13}};
  MockStats defender;
  defender.values = {{"PA", 10}};
  const CheckConfig cfg = combatConfig();
  const CheckParams params;

  // attack nat 1 -> critical success, defender still rolls.
  auto r1 = script({1, 15});
  const CheckResult crit = rpg_os::resolveCheck(attacker, defender, cfg, params, r1);
  CHECK(crit.isSuccess);
  CHECK(crit.isCriticalSuccess);

  // attack nat 20 -> fumble, no parry roll.
  auto r2 = script({20});
  const CheckResult fumble = rpg_os::resolveCheck(attacker, defender, cfg, params, r2);
  CHECK_FALSE(fumble.isSuccess);
  CHECK(fumble.isCriticalFailure);
}

// ---------------------------------------------------------------------------
// Shared helper
// ---------------------------------------------------------------------------

TEST_CASE("Algorithm templates: callable directly with string_view params") {
  // This is the path generated specific-mode code takes: string literals for
  // every parameter, so the compiler can inline and fold everything.
  MockStats geron;
  geron.values = {{"COU", 12}, {"AGI", 13}, {"STR", 11}, {"climbing", 7}};
  const rpg_os::CheckParams params{/*difficulty=*/0, /*situationalModifier=*/0};
  auto rng = script({14, 12, 11});
  const CheckResult result =
      rpg_os::resolveTripleRollUnderPool(geron, "COU", "AGI", "STR", "climbing", params, rng);
  CHECK(result.isSuccess);
  CHECK(result.remainingPool == 5);
}

TEST_CASE("qualityLevelFromRemaining follows the DSA table") {
  CHECK(rpg_os::qualityLevelFromRemaining(0) == 1);
  CHECK(rpg_os::qualityLevelFromRemaining(3) == 1);
  CHECK(rpg_os::qualityLevelFromRemaining(4) == 2);
  CHECK(rpg_os::qualityLevelFromRemaining(6) == 2);
  CHECK(rpg_os::qualityLevelFromRemaining(7) == 3);
  CHECK(rpg_os::qualityLevelFromRemaining(13) == 5);
  CHECK(rpg_os::qualityLevelFromRemaining(16) == 6);
}
