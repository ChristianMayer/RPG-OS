// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Combat simulation helpers (universal mode).
//
// Runs a fight between two entities "to the end" using the ruleset's
// attack-vs-defence check and the event-driven damage pipeline (armour
// absorption, wound checks). Both archetypes and bestiary entries can fight:
// bestiary fields (`attacks[].to_hit`, `attacks[].damage`, `dodge`,
// `armor_rating`) are promoted into stats so the shared check algorithms and
// the damage pipeline see them, while archetypes rely on their derived
// `Attack` / `Parry` values plus a weapon damage expression.
//
// Every call to `runFight` creates fresh entities, so ranged values (e.g. hit
// points rolled from dice) are re-rolled per fight — the building block for
// Monte Carlo simulations such as the ELO ranking in scripts/elo_ranking.py.
#pragma once

#include <cstdint>
#include <memory>
#include <rpg_os/core/dice_engine.hpp>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <string_view>

namespace rpg_os {

/// Static, per-entry description of a combatant. Built once from the ruleset
/// and reused to spawn fresh entities for every fight.
struct CombatantSpec {
  std::string id;
  std::string name;
  bool isArchetype{false};      ///< true: archetype, false: bestiary entry
  int32_t attackValue{0};       ///< to-hit value (Attack / bestiary `to_hit`)
  int32_t defenseValue{0};      ///< parry / bestiary `dodge`
  int32_t armorRating{0};       ///< Armor_Rating (bestiary `armor_rating`)
  std::string damageExpression; ///< dice expression, e.g. "2d6+4"
};

/// The result of one fight.
struct FightOutcome {
  int winnerIndex{-1};          ///< 0 or 1 (index of the combatant passed to runFight), -1 = draw
  int rounds{0};                ///< rounds fought until the decision (maxRounds on a draw)
  int32_t maxLp[2]{0, 0};       ///< starting hit points of both combatants
  int32_t remainingLp[2]{0, 0}; ///< hit points left at the end
};

/// Finds the id of the ruleset's attack-vs-defence check type ("tde_attack"
/// when present, otherwise the first `attack_vs_defense` check). Returns an
/// empty string when the ruleset has none.
[[nodiscard]] inline std::string resolveAttackCheckType(const Ruleset &ruleset) {
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    if (def.id == "tde_attack") {
      return def.id;
    }
  }
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    if (def.config.kind == CheckKind::AttackVsDefense) {
      return def.id;
    }
  }
  return {};
}

/// Finds the id of the ruleset's primary hit-point pool (the first resource
/// pool with a minimum of 0), e.g. "LP" for The Dark Eye and "HP" for D&D 5e.
[[nodiscard]] inline std::string resolveHitPointPool(const Ruleset &ruleset) {
  for (const ResourcePoolDef &pool : ruleset.resourcePools) {
    if (pool.minValue == 0) {
      return pool.id;
    }
  }
  return {};
}

/// Builds a `CombatantSpec` for `id` (archetype first, then bestiary entry).
/// `weaponDamage` is used for archetypes (default: a longsword) and for
/// bestiary entries that have no natural attack defined. Returns false when
/// `id` does not exist in the ruleset's data section.
[[nodiscard]] inline bool makeCombatantSpec(RulesetEngine &engine, std::string_view id,
                                            CombatantSpec &out,
                                            std::string_view weaponDamage = "1d6+4") {
  const Ruleset &ruleset = engine.ruleset();
  if (!ruleset.data.is_object()) {
    return false;
  }
  DefaultRandom probeRng;

  if (ruleset.data.contains("archetypes")) {
    for (const Json &entry : ruleset.data.at("archetypes")) {
      if (entry.value("id", "") != id) {
        continue;
      }
      auto probe = engine.createEntityWith(id, Variance::Average, probeRng);
      if (probe == nullptr) {
        return false;
      }
      out.id = std::string(id);
      out.name = entry.value("name", out.id);
      out.isArchetype = true;
      out.attackValue = probe->getStat("Attack");
      out.defenseValue = probe->getStat("Parry");
      out.armorRating = probe->baseAttribute("Armor_Rating");
      out.damageExpression = std::string(weaponDamage);
      return true;
    }
  }

  if (ruleset.data.contains("creatures")) {
    for (const Json &entry : ruleset.data.at("creatures")) {
      if (entry.value("id", "") != id) {
        continue;
      }
      auto probe = engine.createCreatureWith(id, Variance::Average, probeRng);
      if (probe == nullptr) {
        return false;
      }
      out.id = std::string(id);
      out.name = entry.value("name", out.id);
      out.isArchetype = false;
      out.defenseValue = entry.value("dodge", 0);
      out.armorRating = entry.value("armor_rating", 0);
      out.attackValue = 0;
      out.damageExpression = std::string(weaponDamage);
      if (entry.contains("attacks") && entry.at("attacks").is_array()) {
        int32_t bestToHit = -1;
        for (const Json &attack : entry.at("attacks")) {
          const int32_t toHit = attack.value("to_hit", 0);
          if (toHit > bestToHit) {
            bestToHit = toHit;
            out.attackValue = toHit;
            out.damageExpression = attack.value("damage", "1d6");
          }
        }
      }
      if (out.attackValue <= 0) {
        // No natural attack defined: fall back to the creature's derived
        // attack value and an unarmed strike.
        out.attackValue = probe->getStat("Attack");
        out.damageExpression = "1d6";
      }
      return true;
    }
  }
  return false;
}

/// Creates a fresh entity for a fight from `spec`. Ranged values are picked at
/// the "average" variance so Monte Carlo trials are comparable. Bestiary
/// fields are promoted into the `Attack` / `Parry` / `Armor_Rating` stats the
/// shared check algorithms and the damage pipeline read.
template <RandomNumberGenerator Rng>
[[nodiscard]] std::shared_ptr<DynamicEntity> createFighter(RulesetEngine &engine,
                                                           const CombatantSpec &spec, Rng &rng) {
  std::shared_ptr<DynamicEntity> entity =
      spec.isArchetype ? engine.createEntityWith(spec.id, Variance::Average, rng)
                       : engine.createCreatureWith(spec.id, Variance::Average, rng);
  if (entity != nullptr && !spec.isArchetype) {
    entity->setBaseAttribute("Attack", spec.attackValue);
    entity->setBaseAttribute("Parry", spec.defenseValue);
    entity->setBaseAttribute("Armor_Rating", spec.armorRating);
  }
  return entity;
}

namespace detail {

/// One combatant's action: resolves the attack check against the defender and,
/// on a hit, rolls and applies the damage through the event pipeline. Returns
/// true when the defender is reduced to 0 (or below) hit points.
template <RandomNumberGenerator Rng>
bool attackOnce(RulesetEngine &engine, DynamicEntity &attacker, const DiceExpression &damage,
                DynamicEntity &defender, std::string_view checkTypeId,
                std::string_view hpResourceId, Rng &rng) {
  const CheckResult hit = engine.executeCheck(checkTypeId, attacker, &defender, CheckParams{}, rng);
  if (!hit.isSuccess) {
    return false;
  }
  const int32_t raw = static_cast<int32_t>(damage.rollSum(rng));
  engine.applyDamage(attacker, defender, hpResourceId, raw);
  return defender.resource(hpResourceId) <= 0;
}

} // namespace detail

/// Runs a fight between two freshly created combatants until one of them
/// reaches 0 hit points, or `maxRounds` elapse (a draw). Each round the
/// initiative order is re-rolled as the derived `Initiative` stat plus 1d6; the
/// higher value acts first (ties are decided by a coin flip).
template <RandomNumberGenerator Rng>
[[nodiscard]] FightOutcome runFight(RulesetEngine &engine, const CombatantSpec &a,
                                    const CombatantSpec &b, std::string_view checkTypeId,
                                    std::string_view hpResourceId, int maxRounds, Rng &rng) {
  FightOutcome outcome;
  auto ea = createFighter(engine, a, rng);
  auto eb = createFighter(engine, b, rng);
  if (ea == nullptr || eb == nullptr) {
    return outcome;
  }
  outcome.maxLp[0] = ea->resource(hpResourceId);
  outcome.maxLp[1] = eb->resource(hpResourceId);
  const DiceExpression damageA(a.damageExpression);
  const DiceExpression damageB(b.damageExpression);

  int roundsPlayed = 0;
  for (; roundsPlayed < maxRounds; ++roundsPlayed) {
    const int initA = ea->getStat("Initiative") + rng(1, 6);
    const int initB = eb->getStat("Initiative") + rng(1, 6);
    const bool aFirst = initA != initB ? initA > initB : rng(0, 1) == 0;

    DynamicEntity *first = aFirst ? ea.get() : eb.get();
    DynamicEntity *second = aFirst ? eb.get() : ea.get();
    const DiceExpression &firstDamage = aFirst ? damageA : damageB;
    const DiceExpression &secondDamage = aFirst ? damageB : damageA;

    if (detail::attackOnce(engine, *first, firstDamage, *second, checkTypeId, hpResourceId, rng)) {
      outcome.winnerIndex = aFirst ? 0 : 1;
      break;
    }
    if (detail::attackOnce(engine, *second, secondDamage, *first, checkTypeId, hpResourceId, rng)) {
      outcome.winnerIndex = aFirst ? 1 : 0;
      break;
    }
  }

  outcome.rounds = outcome.winnerIndex == -1 ? maxRounds : roundsPlayed + 1;
  outcome.remainingLp[0] = ea->resource(hpResourceId);
  outcome.remainingLp[1] = eb->resource(hpResourceId);
  return outcome;
}

} // namespace rpg_os
