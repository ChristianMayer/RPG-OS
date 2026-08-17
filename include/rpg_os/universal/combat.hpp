// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file combat.hpp
 * @brief Combat simulation helpers (universal mode).
 *
 * Runs a fight between two entities "to the end" using the ruleset's
 * attack-vs-defence check and the event-driven damage pipeline (armour
 * absorption, wound checks). Both archetypes and bestiary entries can fight:
 * bestiary fields (@c attacks[].to_hit, @c attacks[].damage, @c dodge,
 * @c armor_rating, and for D&D-style systems @c ac and @c initiative) are
 * promoted into stats so the shared check algorithms and the damage pipeline
 * see them, while archetypes rely on their derived @c Attack / @c Parry
 * values plus a weapon damage expression.
 *
 * @par Why a combat simulator in the engine at all?
 * Combat is where the engine's pieces meet: checks, damage, resources, and
 * events. A self-contained fight loop both demonstrates the API and provides
 * the building block for Monte-Carlo tooling (the ELO ranking in
 * @c scripts/elo_ranking.py runs thousands of these fights).
 *
 * @par Why fresh entities per fight?
 * Every call to @c runFight creates new entities, so ranged values (e.g.
 * hit points rolled from dice) are re-rolled per fight. That is exactly what a
 * Monte-Carlo trial needs — each fight is an independent sample from the same
 * distribution, not a replay of a single rolled-out creature.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <rpg_os/core/dice_engine.hpp>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/engine.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace rpg_os {

/**
 * Static, per-entry description of a combatant. Built once from the ruleset
 * and reused to spawn fresh entities for every fight — this is what lets a
 * Monte-Carlo loop avoid re-deriving the stat block on each of thousands of
 * fights.
 *
 * A combatant may also be a spellcaster: @ref spellIds lists the spells it
 * knows (for archetypes, their `spells_known`). When magic is enabled the
 * fight loop prefers casting the strongest affordable damaging spell over
 * swinging a weapon, falling back to a weapon attack only once the caster is
 * out of usable magic. @ref runFight's @c useMagic flag turns this off for a
 * pure weapon-vs-weapon comparison.
 */
struct CombatantSpec {
  std::string id;
  std::string name;
  bool isArchetype{false};           ///< true: archetype, false: bestiary entry
  int32_t attackValue{0};            ///< to-hit value (Attack / bestiary `to_hit`)
  int32_t defenseValue{0};           ///< parry / bestiary `dodge`
  int32_t armorRating{0};            ///< Armor_Rating (bestiary `armor_rating`)
  int32_t acValue{0};                ///< bestiary `ac` (D&D armor class; 0 when absent)
  int32_t initiativeValue{0};        ///< bestiary `initiative` (0 when absent)
  std::string damageExpression;      ///< dice expression, e.g. "2d6+4"
  std::vector<std::string> spellIds; ///< spells the combatant can cast (empty = pure melee)
  /// An optional embedded character sheet (the `DynamicEntity` save form).
  /// When present (a JSON object), @c createFighter builds the fighter from
  /// this sheet instead of the named ruleset entry — letting ANY character
  /// (not just a named archetype or bestiary entry) take part in a fight.
  Json sheet;
};

/// The result of one fight.
struct FightOutcome {
  int winnerIndex{-1}; ///< 0 or 1 (index of the combatant passed to runFight), -1 = draw
  int rounds{0};       ///< rounds fought until the decision (maxRounds on a draw)
  std::array<int32_t, 2> maxLp{0, 0};       ///< starting hit points of both combatants
  std::array<int32_t, 2> remainingLp{0, 0}; ///< hit points left at the end
};

/// Finds the id of the ruleset's opposed combat check type ("tde_attack"
/// when present, otherwise the first check whose resolution is opposed).
/// Returns an empty string when the ruleset has none.
///
/// @par Why a preferred id plus a fallback?
/// The Dark Eye's shipped check is canonically named "tde_attack", but the
/// simulator should also work on any other ruleset that declares an opposed
/// combat check. Preferring the known name, then falling back to "any check
/// with an opposed resolution", keeps both the shipped rulesets and generic
/// ones working without hard-coding a ruleset's naming into the engine.
[[nodiscard]] inline std::string resolveAttackCheckType(const Ruleset &ruleset) {
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    if (def.id == "tde_attack") {
      return def.id;
    }
  }
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    if (def.recipe.resolution == Resolution::Opposed) {
      return def.id;
    }
  }
  // D&D-style attack-vs-AC: a threshold check rolled against a *target* stat
  // (e.g. AC) whose bonus comes from the combatant's promoted `Attack` stat
  // (the bestiary `to_hit`). Systems without an opposed parry roll (D&D)
  // use this instead of the opposed resolution above.
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    if (def.recipe.resolution == Resolution::Threshold &&
        def.recipe.thresholdSource == ThresholdSource::TargetStat &&
        std::ranges::contains(def.recipe.bonusStats, "Attack")) {
      return def.id;
    }
  }
  return {};
}

/// Finds the id of the ruleset's primary hit-point pool (the first resource
/// pool with a minimum of 0), e.g. "LP" for The Dark Eye and "HP" for D&D 5e.
///
/// @par Why "minimum of 0" as the heuristic?
/// The primary hit-point pool is the one a creature is reduced to 0 in to die;
/// in both shipped rulesets it is the pool whose minimum is 0 (LP, HP), while
/// secondary pools (Astral Energy, Karma) start above 0. The heuristic avoids
/// hard-coding pool names into the engine.
[[nodiscard]] inline std::string resolveHitPointPool(const Ruleset &ruleset) {
  for (const ResourcePoolDef &pool : ruleset.resourcePools) {
    if (pool.minValue == 0) {
      return pool.id;
    }
  }
  return {};
}

namespace detail {

/// Fills `out` from an archetype record (a probe entity at average variance);
/// false when the archetype cannot be created.
[[nodiscard]] inline bool fillArchetypeSpec(RulesetEngine &engine, const Json &entry,
                                            std::string_view id, CombatantSpec &out,
                                            std::string_view weaponDamage,
                                            DefaultRandom &probeRng) {
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
  // An archetype's `spells_known` were loaded into the probe's spellbook;
  // copy them so the fight loop can cast magic for this combatant.
  out.spellIds = probe->spellbook().known();
  return true;
}

/// Fills `out` from a bestiary record (a probe creature at average variance),
/// promoting its `to_hit` / `dodge` / `armor_rating` / `ac` / `initiative`
/// fields; false when the creature cannot be created.
[[nodiscard]] inline bool fillCreatureSpec(RulesetEngine &engine, const Json &entry,
                                           std::string_view id, CombatantSpec &out,
                                           std::string_view weaponDamage, DefaultRandom &probeRng) {
  auto probe = engine.createCreatureWith(id, Variance::Average, probeRng);
  if (probe == nullptr) {
    return false;
  }
  out.id = std::string(id);
  out.name = entry.value("name", out.id);
  out.isArchetype = false;
  out.defenseValue = entry.value("dodge", 0);
  out.armorRating = entry.value("armor_rating", 0);
  out.acValue = entry.value("ac", 0);
  out.initiativeValue = entry.value("initiative", 0);
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

} // namespace detail

/// Builds a `CombatantSpec` for `id` (archetype first, then bestiary entry).
/// `weaponDamage` is used for archetypes (default: a longsword) and for
/// bestiary entries that have no natural attack defined. Returns false when
/// `id` does not exist in the ruleset's data section.
///
/// @par Why probe with average variance?
/// The spec needs *representative* combat values, not a random roll (a single
/// unlucky roll would mis-rank a combatant in a tournament). Picking the
/// middle third of each range yields a stable stat block for the whole run
/// while still reflecting the entry's real numbers.
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
      return detail::fillArchetypeSpec(engine, entry, id, out, weaponDamage, probeRng);
    }
  }

  if (ruleset.data.contains("creatures")) {
    for (const Json &entry : ruleset.data.at("creatures")) {
      if (entry.value("id", "") != id) {
        continue;
      }
      return detail::fillCreatureSpec(engine, entry, id, out, weaponDamage, probeRng);
    }
  }
  return false;
}

/// Builds a `CombatantSpec` from an *existing* character sheet (`entity`), so
/// any character — not only a named archetype or bestiary entry — can take
/// part in a fight. The entity's current state is captured into the spec's
/// embedded sheet (see @ref CombatantSpec::sheet), and @c createFighter
/// rebuilds a fresh entity from that sheet for every Monte-Carlo trial.
///
/// @par Why does the spec carry a whole sheet?
/// `runFight` creates new entities per fight. A character that exists only as
/// a sheet (a form entry, a saved game) has no ruleset record to re-derive
/// from, so the spec must carry the sheet itself; the combat values
/// (`Attack`, `Parry`, `Armor_Rating`, `AC`, `Initiative`) are captured too,
/// so callers can inspect the spec without a live entity.
[[nodiscard]] inline bool makeCombatantSpecFromEntity(RulesetEngine &engine,
                                                      const DynamicEntity &entity,
                                                      CombatantSpec &out,
                                                      std::string_view weaponDamage = "1d6+4") {
  out.id = entity.id();
  out.name = entity.id();
  // Resolve a human-readable name when the sheet happens to be a known
  // ruleset entry (archetype or bestiary); otherwise the id is the name.
  const Json *record = engine.findDataRecord("archetypes", entity.id());
  if (record == nullptr) {
    record = engine.findDataRecord("creatures", entity.id());
  }
  if (record != nullptr) {
    out.name = record->value("name", out.name);
  }
  out.isArchetype = true; // the sheet fully describes the character
  out.attackValue = entity.getStat("Attack");
  out.defenseValue = entity.getStat("Parry");
  out.armorRating = entity.getStat("Armor_Rating");
  out.acValue = entity.getStat("AC");
  out.initiativeValue = entity.getStat("Initiative");
  out.damageExpression = std::string(weaponDamage);
  out.spellIds = entity.spellbook().known();
  entity.toJson(out.sheet);
  return true;
}

/// Creates a fresh entity for a fight from `spec`. Ranged values are picked at
/// the "average" variance so Monte Carlo trials are comparable. Bestiary
/// fields are promoted into the `Attack` / `Parry` / `Armor_Rating` stats the
/// shared check algorithms and the damage pipeline read.
///
/// @par Why promote bestiary fields into derived-stat names?
/// The shared check and damage code reads `Attack`, `Parry`, and
/// `Armor_Rating` — names that archetypes get from their derived stats. A
/// bestiary entry has no such derivation, so its raw fields (`to_hit`,
/// `dodge`, `armor_rating`) are written into those same stats. This is the
/// seam that lets one combat loop serve both data shapes.
template <RandomNumberGenerator Rng>
[[nodiscard]] std::shared_ptr<DynamicEntity> createFighter(RulesetEngine &engine,
                                                           const CombatantSpec &spec, Rng &rng) {
  // A spec carrying an embedded sheet fights that sheet directly: ANY
  // character — not just a named archetype or bestiary entry — can take part
  // in a fight this way (e.g. a character entered into a form). The sheet is
  // restored into a fresh entity per fight and its resource pools are
  // (re)created so the fighter starts at its full derived hit points.
  if (spec.sheet.is_object()) {
    auto entity = std::make_shared<DynamicEntity>(engine.ruleset(), spec.id);
    entity->fromJson(spec.sheet);
    entity->refreshResources();
    return entity;
  }
  std::shared_ptr<DynamicEntity> entity =
      spec.isArchetype ? engine.createEntityWith(spec.id, Variance::Average, rng)
                       : engine.createCreatureWith(spec.id, Variance::Average, rng);
  if (entity != nullptr && !spec.isArchetype) {
    entity->setBaseAttribute("Attack", spec.attackValue);
    entity->setBaseAttribute("Parry", spec.defenseValue);
    entity->setBaseAttribute("Armor_Rating", spec.armorRating);
    // D&D-style systems promote the bestiary's real AC and initiative into
    // base attributes: the derived `AC = 10 + DEX_mod` does not match a
    // creature's stat block, and `Initiative` has no derived stat at all.
    // Skipping a zero value keeps rulesets without those fields (TDE, BRP)
    // on their derived stats.
    if (spec.acValue != 0) {
      entity->setBaseAttribute("AC", spec.acValue);
    }
    if (spec.initiativeValue != 0) {
      entity->setBaseAttribute("Initiative", spec.initiativeValue);
    }
  }
  return entity;
}

namespace detail {

/// The expected damage of a spell record, used only to rank spells so the
/// caster picks its strongest option. Handles both the bare `damage` field
/// and the structured `effects` array (sums the `dice` of every damage-kind
/// effect). A spell with no damage at all (a buff, a heal, a utility effect)
/// ranks as zero and is never chosen as a fight action.
[[nodiscard]] inline double spellAverageDamage(const Json &spell) {
  double total = 0.0;
  const auto addDice = [&](const Json &value) {
    if (value.is_number()) {
      total += value.get<double>();
    } else if (value.is_string()) {
      total += DiceExpression(value.get<std::string>()).expectedValue();
    }
  };
  if (spell.contains("damage")) {
    addDice(spell.at("damage"));
  }
  if (spell.contains("effects") && spell.at("effects").is_array()) {
    for (const Json &effect : spell.at("effects")) {
      if (effect.value("kind", "") == "damage" && effect.contains("dice")) {
        addDice(effect.at("dice"));
      }
    }
  }
  return total;
}

/// Returns the id of the strongest affordable damaging spell `caster` can cast
/// from `spellIds`, or an empty string when none is usable.
///
/// @par Why "affordable" and "damaging"?
/// A fight is a damage race: a spellcaster is only "magic" while it has a
/// spell it can actually afford to cast and that actually hurts. The cost is
/// read exactly as @ref RulesetEngine::castSpell reads it (`cost`, `ae_cost`,
/// then `level`), so the affordability check cannot disagree with what casting
/// would really spend.
[[nodiscard]] inline std::string pickSpell(const RulesetEngine &engine, const DynamicEntity &caster,
                                           const std::vector<std::string> &spellIds) {
  const std::string &resourceId = engine.ruleset().spellResource;
  std::string best;
  double bestDamage = 0.0;
  for (const std::string &spellId : spellIds) {
    const Json *spell = engine.findSpell(spellId);
    if (spell == nullptr) {
      continue;
    }
    int32_t cost = 0;
    if (spell->contains("cost") && spell->at("cost").is_number_integer()) {
      cost = spell->at("cost").get<int32_t>();
    } else if (spell->contains("ae_cost") && spell->at("ae_cost").is_number_integer()) {
      cost = spell->at("ae_cost").get<int32_t>();
    } else if (spell->contains("level") && spell->at("level").is_number_integer()) {
      cost = spell->at("level").get<int32_t>();
    }
    if (cost > 0 && !resourceId.empty() && caster.resource(resourceId) < cost) {
      continue; // cannot afford
    }
    const double damage = spellAverageDamage(*spell);
    if (damage > 0.0 && damage > bestDamage) {
      bestDamage = damage;
      best = spellId;
    }
  }
  return best;
}

/// One combatant's action: resolves the attack check against the defender and,
/// on a hit, rolls and applies the damage through the event pipeline. Returns
/// true when the defender is reduced to 0 (or below) hit points.
///
/// @par Why keep this in a detail namespace?
/// It is a single round of a fight — meaningful only inside the loop below.
/// Hiding it in @c detail keeps the public surface of the header to the
/// reusable entry points (@ref makeCombatantSpec, @ref runFight) while still
/// making the loop readable.
template <RandomNumberGenerator Rng>
bool attackOnce(RulesetEngine &engine, DynamicEntity &attacker, const DiceExpression &damage,
                DynamicEntity &defender, std::string_view checkTypeId,
                std::string_view hpResourceId, Rng &rng) {
  // The attacker's restrictions are enforced: a creature that cannot take
  // actions (incapacitated, stunned, paralyzed, ...) cannot attack.
  if (!engine.actionAllowed(attacker, "action")) {
    return false;
  }
  // Resolve against effective stats so worn gear and active conditions (armour,
  // encumbrance, wounds) influence the fight — with nothing equipped the
  // effective value equals the raw value, so ungeared fights are unchanged.
  const CheckResult hit =
      engine.executeCheckEffective(checkTypeId, attacker, &defender, CheckParams{}, rng);
  if (!hit.isSuccess) {
    return false;
  }
  const int32_t raw = static_cast<int32_t>(damage.rollSum(rng));
  engine.applyDamage(attacker, defender, hpResourceId, raw);
  return defender.resource(hpResourceId) <= 0;
}

/// One combatant's action for a fight: prefer casting the strongest affordable
/// damaging spell when magic is enabled and a usable spell exists, otherwise
/// make a weapon attack. Returns true when the defender is reduced to 0 hit
/// points.
///
/// @par Why does a failed casting attempt consume the action?
/// Casting is what a spellcaster does instead of swinging a weapon (in The
/// Dark Eye a spell takes a full action), and the arcane energy is spent on
/// the attempt even when the check fails — so a botched cast simply wastes
/// the round. Only when *no* spell is usable (nothing known, or all spells
/// unaffordable) does the combatant fall back to its weapon.
template <RandomNumberGenerator Rng>
bool actOnce(RulesetEngine &engine, DynamicEntity &actor, const CombatantSpec &spec,
             const DiceExpression &weaponDamage, DynamicEntity &defender,
             std::string_view checkTypeId, std::string_view hpResourceId, bool useMagic, Rng &rng) {
  if (useMagic && !spec.spellIds.empty()) {
    const std::string spellId = pickSpell(engine, actor, spec.spellIds);
    if (!spellId.empty()) {
      const auto cast = engine.castSpell(spellId, actor, &defender, CheckParams{}, rng);
      if (cast.cast) {
        return defender.resource(hpResourceId) <= 0;
      }
      return false; // the casting attempt failed — the round is spent
    }
  }
  return attackOnce(engine, actor, weaponDamage, defender, checkTypeId, hpResourceId, rng);
}

/// Plays one round of a fight: rolls initiative, resolves both combatants'
/// actions in initiative order, and returns the winner's index (0 or 1) or
/// -1 when neither combatant is reduced to 0 hit points.
///
/// @par Why return the winner instead of mutating the outcome?
/// Keeping the round self-contained (inputs in, winner out) lets the fight
/// loop stay a flat read loop — no winner bookkeeping spread across the round
/// body — and makes the per-round RNG stream easy to reason about in tests.
template <RandomNumberGenerator Rng>
[[nodiscard]] int resolveRound(RulesetEngine &engine, const CombatantSpec &a,
                               const CombatantSpec &b, DynamicEntity &ea, DynamicEntity &eb,
                               const DiceExpression &damageA, const DiceExpression &damageB,
                               std::string_view checkTypeId, std::string_view hpResourceId,
                               bool useMagic, Rng &rng) {
  const int initA = ea.getStat("Initiative") + rng(1, 6);
  const int initB = eb.getStat("Initiative") + rng(1, 6);
  const bool aFirst = initA != initB ? initA > initB : rng(0, 1) == 0;

  DynamicEntity &first = aFirst ? ea : eb;
  DynamicEntity &second = aFirst ? eb : ea;
  const CombatantSpec &firstSpec = aFirst ? a : b;
  const CombatantSpec &secondSpec = aFirst ? b : a;
  const DiceExpression &firstDamage = aFirst ? damageA : damageB;
  const DiceExpression &secondDamage = aFirst ? damageB : damageA;

  if (actOnce(engine, first, firstSpec, firstDamage, second, checkTypeId, hpResourceId, useMagic,
              rng)) {
    return aFirst ? 0 : 1;
  }
  if (actOnce(engine, second, secondSpec, secondDamage, first, checkTypeId, hpResourceId, useMagic,
              rng)) {
    return aFirst ? 1 : 0;
  }
  return -1;
}

} // namespace detail

/// Runs a fight between two freshly created combatants until one of them
/// reaches 0 hit points, or `maxRounds` elapse (a draw). Each round the
/// initiative order is re-rolled as the derived `Initiative` stat plus 1d6; the
/// higher value acts first (ties are decided by a coin flip).
///
/// With @c useMagic (the default) a combatant that knows spells casts its
/// strongest affordable damaging spell instead of attacking with a weapon,
/// falling back to a weapon only when it has no usable spell left. Pass
/// @c false for a pure weapon-vs-weapon comparison (e.g. when the ELO ranking
/// should measure physical combat only).
///
/// @par Why re-roll initiative every round?
/// In the source rules initiative can change from round to round (a character
/// may act more slowly when wounded). Re-rolling per round makes the
/// simulation reflect that dynamism rather than freezing turn order from round
/// one — a choice that materially changes who gets to strike first in a long
/// fight, and therefore the outcome distribution.
template <RandomNumberGenerator Rng>
[[nodiscard]] FightOutcome runFight(RulesetEngine &engine, const CombatantSpec &a,
                                    const CombatantSpec &b, std::string_view checkTypeId,
                                    std::string_view hpResourceId, int maxRounds, Rng &rng,
                                    bool useMagic = true) {
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
    const int winner = detail::resolveRound(engine, a, b, *ea, *eb, damageA, damageB, checkTypeId,
                                            hpResourceId, useMagic, rng);
    if (winner >= 0) {
      outcome.winnerIndex = winner;
      break;
    }
  }

  outcome.rounds = outcome.winnerIndex == -1 ? maxRounds : roundsPlayed + 1;
  outcome.remainingLp[0] = ea->resource(hpResourceId);
  outcome.remainingLp[1] = eb->resource(hpResourceId);
  return outcome;
}

} // namespace rpg_os
