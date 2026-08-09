// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Universal mode demo: load any ruleset JSON at runtime and resolve rules
// generically. Nothing here is tied to a specific game system.
//
// Usage: rpg_os_example_universal [project-root]
#include <iostream>
#include <rpg_os/universal/engine.hpp>
#include <string>

namespace {

std::string rulesetPath(std::string_view root, std::string_view name) {
  return std::string(root) + "/rulesets/" + std::string(name);
}

void runTdeExample(const std::string &root) {
  std::cout << "=== The Dark Eye 5e (universal mode) ===\n";
  rpg_os::RulesetEngine engine;
  if (!engine.loadRulesetFromFile(rulesetPath(root, "tde5e_core.json"))) {
    std::cerr << "failed to load tde5e_core.json: " << engine.lastError() << '\n';
    return;
  }
  auto geron = engine.createEntity("geron");
  if (!geron) {
    std::cerr << "archetype 'geron' not found\n";
    return;
  }
  std::cout << "Geron: COU " << engine.calculateStat(*geron, "COU") << ", Life Points "
            << geron->resource("LP") << "/" << engine.calculateStat(*geron, "LifePoints_Max")
            << ", Attack " << engine.calculateStat(*geron, "Attack") << ", Dodge "
            << engine.calculateStat(*geron, "Dodge") << '\n';

  // A climbing skill check (linked attributes COU/AGI/STR; rating is the pool).
  const rpg_os::CheckResult climb =
      engine.executeSkillCheck("climbing", *geron, rpg_os::CheckParams{});
  std::cout << "Climbing check: " << (climb.isSuccess ? "success" : "failure") << " (SP left "
            << climb.remainingPool << ", QL " << climb.qualityLevel << ")\n";
}

void runDndExample(const std::string &root) {
  std::cout << "=== D&D 5e SRD (universal mode) ===\n";
  rpg_os::RulesetEngine engine;
  if (!engine.loadRulesetFromFile(rulesetPath(root, "dnd5e_srd.json"))) {
    std::cerr << "failed to load dnd5e_srd.json: " << engine.lastError() << '\n';
    return;
  }
  auto fighter = engine.createEntity("fighter_lvl1");
  if (!fighter) {
    std::cerr << "archetype 'fighter_lvl1' not found\n";
    return;
  }
  std::cout << "Fighter: STR " << engine.calculateStat(*fighter, "STR") << " (mod "
            << engine.calculateStat(*fighter, "STR_mod") << "), AC "
            << engine.calculateStat(*fighter, "AC") << ", HP " << fighter->resource("HP") << '\n';

  // A melee attack roll: 1d20 + STR mod + proficiency vs the target's AC.
  const rpg_os::CheckResult attack =
      engine.executeCheck("dnd5e_attack_melee", *fighter, fighter.get(), rpg_os::CheckParams{});
  std::cout << "Melee attack vs AC " << engine.calculateStat(*fighter, "AC") << ": "
            << (attack.isSuccess ? "hit" : "miss") << " (roll "
            << (attack.rawDiceRolls.empty() ? 0 : attack.rawDiceRolls.front()) << ")\n";
}

} // namespace

int main(int argc, char **argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  runTdeExample(root);
  runDndExample(root);
  return 0;
}
