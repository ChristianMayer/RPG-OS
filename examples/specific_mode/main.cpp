// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Specific mode demo: use the code-generated, strongly typed headers. Attributes
// are named members, derived stats are named getters with compiled formulas,
// and checks are named methods — while the character data still comes from the
// ruleset JSON at runtime.
//
// Usage: rpg_os_example_specific [project-root]
#include <dnd5e_srd_static.hpp>
#include <fstream>
#include <iostream>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <string>
#include <tde5e_core_static.hpp>

namespace {

std::string rulesetPath(std::string_view root, std::string_view name) {
  return std::string(root) + "/rulesets/" + std::string(name);
}

rpg_os::Json loadRuleset(const std::string &root, std::string_view name) {
  std::ifstream file(rulesetPath(root, name));
  return rpg_os::Json::parse(
      std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";

  std::cout << "=== The Dark Eye 5e (specific mode) ===\n";
  const rpg_os::Json tde = loadRuleset(root, "tde5e_core.json");
  const auto geron = rpg_os::generated::tde5e::Character::fromArchetype(tde, "geron");
  std::cout << "Geron: COU " << geron.courage << ", Life Points " << geron.lifePoints << '/'
            << geron.maxLifePoints() << ", Attack " << geron.attackSwordsSr6() << ", Dodge "
            << geron.dodge() << '\n';

  rpg_os::CheckParams params;
  rpg_os::DefaultRandom rng;
  const rpg_os::CheckResult climb = geron.checkClimbing(params, rng);
  std::cout << "Climbing check: " << (climb.isSuccess ? "success" : "failure") << " (SP left "
            << climb.remainingPool << ", QL " << climb.qualityLevel << ")\n";

  std::cout << "=== D&D 5e SRD (specific mode) ===\n";
  const rpg_os::Json dnd = loadRuleset(root, "dnd5e_srd.json");
  const auto fighter = rpg_os::generated::dnd5e::Character::fromArchetype(dnd, "fighter_lvl1");
  std::cout << "Fighter: STR " << fighter.strength << " (mod " << fighter.strengthModifier()
            << "), AC " << fighter.armorClass() << ", HP " << fighter.hitPoints << '\n';

  rpg_os::CheckParams attackParams;
  rpg_os::DefaultRandom attackRng;
  const rpg_os::CheckResult attack = fighter.dnd5eAttackMelee(fighter, attackParams, attackRng);
  std::cout << "Melee attack vs AC " << fighter.armorClass() << ": "
            << (attack.isSuccess ? "hit" : "miss") << " (roll "
            << (attack.rawDiceRolls.empty() ? 0 : attack.rawDiceRolls.front()) << ")\n";
  return 0;
}
