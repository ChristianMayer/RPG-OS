// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_check_resolver.cpp
 * @brief Unit tests for the named-check dispatch seam (@c rpg_os::CheckResolver).
 *
 * @c CheckResolver answers exactly "given a ruleset and a check name, resolve
 * this check" — the name-to-recipe lookup and dispatch the engine facade
 * composes. These tests pin that lookup with a minimal inline ruleset and a
 * scripted RNG, including the throw on an unknown check name.
 */
#include "test_util.hpp"

#include <doctest/doctest.h>
#include <rpg_os/universal/check_resolver.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <string_view>

using rpg_os::CheckParams;
using rpg_os::CheckResolver;
using rpg_os::Ruleset;
using rpg_os::RulesetLoader;

namespace {

constexpr std::string_view kRuleset = R"json(
{
  "schema_version": 1,
  "ruleset_id": "resolver_demo",
  "ruleset_name": "Resolver Demo",
  "licence": "test",
  "namespace": "rpg_os::generated::resolver_demo",
  "attributes": [
    { "id": "STR", "name": "Strength", "min": 1, "max": 21, "default": 10 },
    { "id": "DEF", "name": "Defense", "min": 1, "max": 21, "default": 10 }
  ],
  "check_types": {
    "hit": {
      "resolution": "threshold",
      "dice": "1d20",
      "comparison": "ge",
      "threshold_source": "difficulty",
      "bonus_stats": ["STR"]
    },
    "fight": {
      "resolution": "opposed",
      "dice": "1d20",
      "attack_stat": "STR",
      "parry_stat": "DEF"
    }
  },
  "skills": [],
  "cost_tables": {},
  "equipment_slots": [],
  "event_triggers": []
}
)json";

} // namespace

TEST_CASE("check_resolver: resolves a named threshold check against a difficulty") {
  const Ruleset ruleset = RulesetLoader::load(rpg_os::Json::parse(kRuleset));
  MockStats actor;
  actor.values["STR"] = 14;
  const MockStats noTarget; // a solo threshold check has no target
  CheckParams params;
  params.difficulty = 20;

  // 10 + 14 = 24 >= 20 -> success with margin 4.
  ScriptedRng okRng = script({10});
  const rpg_os::CheckResult ok =
      CheckResolver::resolve(ruleset, actor, noTarget, "hit", params, okRng);
  CHECK(ok.isSuccess);
  CHECK(ok.marginOfSuccess == 4);
  CHECK(ok.rawDiceRolls.size() == 1);

  // 2 + 14 = 16 < 20 -> failure with margin -4.
  ScriptedRng failRng = script({2});
  const rpg_os::CheckResult fail =
      CheckResolver::resolve(ruleset, actor, noTarget, "hit", params, failRng);
  CHECK_FALSE(fail.isSuccess);
  CHECK(fail.marginOfSuccess == -4);
}

TEST_CASE("check_resolver: resolves a named opposed check with a target") {
  const Ruleset ruleset = RulesetLoader::load(rpg_os::Json::parse(kRuleset));
  MockStats actor;
  actor.values["STR"] = 14;
  MockStats target;
  target.values["DEF"] = 10;
  CheckParams params;

  // Staged contest: attack 8 <= 14 hits, parry 12 > 10 fails -> the blow lands.
  ScriptedRng hitRng = script({8, 12});
  const rpg_os::CheckResult hit =
      CheckResolver::resolve(ruleset, actor, target, "fight", params, hitRng);
  CHECK(hit.isSuccess);
  CHECK(hit.rawDiceRolls.size() == 2);

  // Attack 8 hits but parry 5 <= 10 succeeds -> the blow is parried.
  ScriptedRng parriedRng = script({8, 5});
  const rpg_os::CheckResult parried =
      CheckResolver::resolve(ruleset, actor, target, "fight", params, parriedRng);
  CHECK_FALSE(parried.isSuccess);
}

TEST_CASE("check_resolver: unknown check type throws") {
  const Ruleset ruleset = RulesetLoader::load(rpg_os::Json::parse(kRuleset));
  MockStats actor;
  const MockStats noTarget;
  CheckParams params;
  ScriptedRng rng = script({10});
  CHECK_THROWS_AS(CheckResolver::resolve(ruleset, actor, noTarget, "no_such_check", params, rng),
                  std::invalid_argument);
}
