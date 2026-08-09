// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file main.cpp
 * @brief Universal mode demo: load any ruleset JSON at runtime and resolve
 * rules generically.
 *
 * Nothing here is tied to a specific game system: the same
 * @c rpg_os::RulesetEngine instance pattern loads either shipped ruleset
 * and drives it purely through ids ("COU", "climbing", "dnd5e_attack_melee").
 * This is the counterpart to @c examples/specific_mode/main.cpp, which
 * shows the same scenarios through the generated, strongly typed headers.
 *
 * @par Why two examples of the same thing?
 * They demonstrate the two halves of the library's promise: universal mode
 * needs zero compilation for a new ruleset, specific mode trades that
 * flexibility for compile-time safety and performance. Running both side by
 * side (they print matching numbers) is the human version of the parity test.
 *
 * Usage: @c rpg_os_example_universal [project-root]
 */
#include <iostream>
#include <rpg_os/universal/engine.hpp>
#include <string>

namespace {

/// Joins the project root with a ruleset file name. Kept as a helper because
/// both examples below load two files and the root comes from argv, so a
/// single path builder keeps the "where are the rulesets?" logic in one spot.
std::string rulesetPath(std::string_view root, std::string_view name) {
  return std::string(root) + "/rulesets/" + std::string(name);
}

/// Demonstrates The Dark Eye 5e: entity creation from an archetype, derived
/// stat calculation, and a real 3d20 talent check (Climbing vs COU/AGI/STR).
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

/// Demonstrates D&D 5e SRD: derived stat calculation (STR mod, AC) and a
/// melee attack roll (1d20 + STR mod + proficiency vs. the target's AC).
void runDndExample(const std::string &root) {
  std::cout << "=== D&D 5e SRD (universal mode) ===\n";
  rpg_os::RulesetEngine engine;
  if (!engine.loadRulesetFromFile(rulesetPath(root, "dnd5e_srd.json"))) {
    std::cerr << "failed to load dnd5e_srd.json: " << engine.lastError() << '\n';
    return;
  }
  // The SRD ruleset stores text descriptions (classes/species/backgrounds/feats)
  // rather than PC archetype blocks, so load a bestiary entry instead.
  auto fighter = engine.createCreature("goblin_warrior");
  if (!fighter) {
    std::cerr << "creature 'goblin_warrior' not found\n";
    return;
  }
  std::cout << "Goblin: STR " << engine.calculateStat(*fighter, "STR") << " (mod "
            << engine.calculateStat(*fighter, "STR_mod") << "), AC "
            << engine.calculateStat(*fighter, "AC") << ", HP " << fighter->resource("HP") << '\n';

  // A melee attack roll: 1d20 + STR mod + proficiency vs the target's AC.
  // Attacking itself with no target entity demonstrates using a dynamic
  // entity as both actor and target — the AC read from the same character.
  const rpg_os::CheckResult attack =
      engine.executeCheck("dnd5e_attack_melee", *fighter, fighter.get(), rpg_os::CheckParams{});
  std::cout << "Melee attack vs AC " << engine.calculateStat(*fighter, "AC") << ": "
            << (attack.isSuccess ? "hit" : "miss") << " (roll "
            << (attack.rawDiceRolls.empty() ? 0 : attack.rawDiceRolls.front()) << ")\n";
}

} // namespace

int main(int argc, char **argv) {
  // Optional leading positional argument = project root (matches the sibling
  // examples' convention so all three demos can be run identically).
  const std::string root = argc > 1 ? argv[1] : ".";
  runTdeExample(root);
  runDndExample(root);
  return 0;
}
