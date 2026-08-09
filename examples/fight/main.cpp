// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Fight-to-the-end demo (universal mode).
//
// Picks two combatants from the ruleset — archetypes or bestiary entries — and
// simulates a fight until one of them drops to 0 hit points, then announces
// the winner. Combatants can be named on the command line, or omitted to fall
// back to a default pair (Geron the Mercenary vs. a Gotongi).
//
// The --csv mode prints one tab-separated line per fight, which is what the
// Monte Carlo ELO ranking (scripts/elo_ranking.py) consumes.
//
// Usage: rpg_os_example_fight [options] [combatant_a] [combatant_b]
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <rpg_os/core/dice_engine.hpp>
#include <rpg_os/universal/combat.hpp>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <vector>

namespace {

struct Options {
  std::string root{"."};
  std::string ruleset; // empty -> derived from root
  std::vector<std::string> combatants;
  bool rootExplicit{false};
  bool listOnly{false};
  bool csv{false};
  std::optional<uint32_t> seed; // nullopt = random OS entropy
  int maxRounds{1000};
  int batch{1};
  std::string weapon{"1d6+4"};
};

void printHelp(std::ostream &os) {
  os << "Usage: rpg_os_example_fight [options] [combatant_a] [combatant_b]\n"
     << "\n"
     << "Fight two entities to the end and announce the winner. Combatants are\n"
     << "ruleset archetypes or bestiary entries; if none are given a default\n"
     << "pair is used.\n"
     << "\n"
     << "Options:\n"
     << "  -h, --help      show this help and exit\n"
     << "  --list          list all combatants in the ruleset and exit\n"
     << "  --root <dir>    project root containing rulesets/ (default: \".\")\n"
     << "  --ruleset <f>   ruleset file (default: <root>/rulesets/tde5e_core.json)\n"
     << "  --seed <n>      RNG seed for reproducible fights (default: random OS entropy)\n"
     << "  --rounds <n>    max rounds before a fight counts as a draw (default: 1000)\n"
     << "  --batch <n>     run n fights between the same pair (default: 1)\n"
     << "  --weapon <dice> weapon damage for archetypes (default: \"1d6+4\")\n"
     << "  --csv           one tab-separated line per fight (for scripts)\n";
}

bool parseArgs(int argc, char **argv, Options &opts, std::string &error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    const auto nextValue = [&](const std::string &flag) -> std::optional<std::string> {
      if (i + 1 >= argc) {
        error = "missing value for " + flag;
        return std::nullopt;
      }
      return std::string(argv[++i]);
    };
    if (arg == "--root") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.root = *value;
      opts.rootExplicit = true;
    } else if (arg == "--ruleset") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.ruleset = *value;
    } else if (arg == "--seed") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.seed = static_cast<uint32_t>(std::stoul(*value));
    } else if (arg == "--rounds") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.maxRounds = std::stoi(*value);
    } else if (arg == "--batch") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.batch = std::stoi(*value);
    } else if (arg == "--weapon") {
      const auto value = nextValue(arg);
      if (!value) {
        return false;
      }
      opts.weapon = *value;
    } else if (arg == "--csv") {
      opts.csv = true;
    } else if (arg == "--list") {
      opts.listOnly = true;
    } else if (arg == "-h" || arg == "--help") {
      printHelp(std::cout);
      std::exit(0);
    } else if (!arg.empty() && arg[0] == '-') {
      error = "unknown option '" + arg + "'";
      return false;
    } else {
      opts.combatants.push_back(arg);
    }
  }
  return true;
}

void printCombatantList(const rpg_os::RulesetEngine &engine) {
  const rpg_os::Json &data = engine.ruleset().data;
  if (data.contains("archetypes")) {
    for (const auto &entry : data.at("archetypes")) {
      std::cout << entry.value("id", "") << '\t' << entry.value("name", "") << '\n';
    }
  }
  if (data.contains("creatures")) {
    for (const auto &entry : data.at("creatures")) {
      std::cout << entry.value("id", "") << '\t' << entry.value("name", "") << '\n';
    }
  }
}

void printCombatants(std::ostream &os, const rpg_os::CombatantSpec &a,
                     const rpg_os::CombatantSpec &b) {
  os << "A: " << a.name << " (Attack " << a.attackValue << ", Defence " << a.defenseValue
     << ", Armour " << a.armorRating << ", \"" << a.damageExpression << "\")\n";
  os << "B: " << b.name << " (Attack " << b.attackValue << ", Defence " << b.defenseValue
     << ", Armour " << b.armorRating << ", \"" << b.damageExpression << "\")\n\n";
}

void printCsvLine(std::ostream &os, const rpg_os::CombatantSpec &a, const rpg_os::CombatantSpec &b,
                  const rpg_os::FightOutcome &outcome) {
  if (outcome.winnerIndex == 0) {
    os << a.id << '\t' << b.id << '\t' << outcome.remainingLp[0] << '\t' << outcome.remainingLp[1]
       << '\t' << outcome.rounds << '\n';
  } else if (outcome.winnerIndex == 1) {
    os << b.id << '\t' << a.id << '\t' << outcome.remainingLp[1] << '\t' << outcome.remainingLp[0]
       << '\t' << outcome.rounds << '\n';
  } else {
    os << "DRAW\t" << a.id << '\t' << b.id << '\t' << outcome.remainingLp[0] << '\t'
       << outcome.remainingLp[1] << '\t' << outcome.rounds << '\n';
  }
}

void printHumanLine(std::ostream &os, const rpg_os::CombatantSpec &a,
                    const rpg_os::CombatantSpec &b, const rpg_os::FightOutcome &outcome) {
  const char *plural = outcome.rounds == 1 ? "" : "s";
  if (outcome.winnerIndex == 0) {
    os << a.name << " defeats " << b.name << " in " << outcome.rounds << " round" << plural << " ("
       << a.name << ' ' << outcome.remainingLp[0] << '/' << outcome.maxLp[0] << " LP, " << b.name
       << ' ' << outcome.remainingLp[1] << '/' << outcome.maxLp[1] << " LP).\n";
  } else if (outcome.winnerIndex == 1) {
    os << b.name << " defeats " << a.name << " in " << outcome.rounds << " round" << plural << " ("
       << b.name << ' ' << outcome.remainingLp[1] << '/' << outcome.maxLp[1] << " LP, " << a.name
       << ' ' << outcome.remainingLp[0] << '/' << outcome.maxLp[0] << " LP).\n";
  } else {
    os << "Draw: " << a.name << " vs " << b.name << " after " << outcome.rounds << " rounds ("
       << a.name << ' ' << outcome.remainingLp[0] << '/' << outcome.maxLp[0] << " LP, " << b.name
       << ' ' << outcome.remainingLp[1] << '/' << outcome.maxLp[1] << " LP).\n";
  }
}

} // namespace

int main(int argc, char **argv) {
  Options opts;
  std::string parseError;
  if (!parseArgs(argc, argv, opts, parseError)) {
    std::cerr << "error: " << parseError << "\n\n";
    printHelp(std::cerr);
    return 2;
  }

  // Match the sibling examples' `[project-root]` convention: when the first
  // positional argument is an existing directory and --root was not given
  // explicitly, treat it as the project root instead of a combatant id.
  if (!opts.rootExplicit && !opts.combatants.empty() &&
      std::filesystem::is_directory(opts.combatants.front())) {
    opts.root = opts.combatants.front();
    opts.combatants.erase(opts.combatants.begin());
  }

  const std::string rulesetPath =
      opts.ruleset.empty() ? opts.root + "/rulesets/tde5e_core.json" : opts.ruleset;

  rpg_os::RulesetEngine engine;
  if (!engine.loadRulesetFromFile(rulesetPath)) {
    std::cerr << "error: failed to load '" << rulesetPath << "': " << engine.lastError() << '\n';
    return 1;
  }

  if (opts.listOnly) {
    printCombatantList(engine);
    return 0;
  }

  const std::string checkType = rpg_os::resolveAttackCheckType(engine.ruleset());
  if (checkType.empty()) {
    std::cerr << "error: ruleset '" << rulesetPath << "' has no attack-vs-defence check type\n";
    return 1;
  }
  const std::string hpPool = rpg_os::resolveHitPointPool(engine.ruleset());
  if (hpPool.empty()) {
    std::cerr << "error: ruleset '" << rulesetPath << "' has no hit-point pool\n";
    return 1;
  }

  std::string idA;
  std::string idB;
  if (opts.combatants.size() == 2) {
    idA = opts.combatants[0];
    idB = opts.combatants[1];
  } else if (opts.combatants.empty()) {
    idA = "geron";
    idB = "gotongi";
  } else {
    std::cerr << "error: expected exactly two combatant ids (got " << opts.combatants.size()
              << ")\n";
    return 2;
  }

  rpg_os::CombatantSpec specA;
  rpg_os::CombatantSpec specB;
  if (!rpg_os::makeCombatantSpec(engine, idA, specA, opts.weapon)) {
    std::cerr << "error: unknown combatant '" << idA << "' (see --list)\n";
    return 1;
  }
  if (!rpg_os::makeCombatantSpec(engine, idB, specB, opts.weapon)) {
    std::cerr << "error: unknown combatant '" << idB << "' (see --list)\n";
    return 1;
  }

  // Without --seed the engine is seeded from real OS entropy, so every run
  // differs; with --seed the run is exactly reproducible.
  rpg_os::DefaultRandom rng =
      opts.seed ? rpg_os::DefaultRandom(*opts.seed) : rpg_os::DefaultRandom{};
  for (int i = 0; i < opts.batch; ++i) {
    const rpg_os::FightOutcome outcome =
        rpg_os::runFight(engine, specA, specB, checkType, hpPool, opts.maxRounds, rng);
    if (opts.csv) {
      printCsvLine(std::cout, specA, specB, outcome);
    } else {
      if (i == 0) {
        printCombatants(std::cout, specA, specB);
      }
      printHumanLine(std::cout, specA, specB, outcome);
    }
  }
  return 0;
}
