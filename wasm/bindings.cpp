// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file bindings.cpp
 * @brief Emscripten (WebAssembly) bindings for the universal RPG-OS engine.
 *
 * A flat extern "C" API that compiles the header-only universal engine to
 * WASM for the npm package `@mundus-mirabilis/rpg-os`. The ruleset JSON is
 * never baked into the binary: the host reads it (Node `fs`, browser
 * `fetch`) and hands it over as a string via @c rpg_os_engine_load_json.
 *
 * @par Memory / lifetime contract
 * Handles are raw pointers the caller owns: every @c *_new / @c *_create /
 * @c *_from_* result must be released with its matching @c *_free. String and
 * JSON results are returned as pointers into one process-global buffer that
 * is overwritten on every call — WASM is single-threaded, so the JS wrapper
 * must copy a result before invoking the next exported function.
 *
 * This translation unit deliberately uses no Emscripten headers unless it is
 * actually compiled under Emscripten, so it can be compiled on a host
 * toolchain (e.g. `g++ -std=c++23 -fsyntax-only`) as a syntax check.
 */

#include <cstdint>
#include <new>
#include <string>

#include <rpg_os/universal/combat.hpp>
#include <rpg_os/universal/dynamic_entity.hpp>
#include <rpg_os/universal/engine.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define RPG_OS_WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define RPG_OS_WASM_EXPORT
#endif

namespace {

/// One shared buffer for every string/JSON result. Overwritten on each call;
/// the JS side copies immediately (see the file comment).
thread_local std::string g_buffer;

/// Stores `value` in the shared buffer and returns it as a C string.
const char *buffered(const std::string &value) {
  g_buffer = value;
  return g_buffer.c_str();
}

/// Parses `[data, len)` as JSON; an empty object on failure or bad input.
rpg_os::Json parseJson(const char *data, int len) {
  if (data == nullptr || len <= 0) {
    return rpg_os::Json::object();
  }
  try {
    return rpg_os::Json::parse(std::string(data, static_cast<std::size_t>(len)));
  } catch (const std::exception &) {
    return rpg_os::Json::object();
  }
}

/// Maps the numeric variance id used by the JS API onto the engine enum.
rpg_os::Variance toVariance(int variance) {
  switch (variance) {
  case 0:
    return rpg_os::Variance::Weakest;
  case 1:
    return rpg_os::Variance::Weak;
  case 2:
    return rpg_os::Variance::Average;
  case 3:
    return rpg_os::Variance::Strong;
  case 4:
    return rpg_os::Variance::Strongest;
  default:
    return rpg_os::Variance::Random;
  }
}

} // namespace

extern "C" {

// ---- Engine lifecycle ------------------------------------------------------

/// Creates a new engine instance (no ruleset loaded yet).
RPG_OS_WASM_EXPORT void *rpg_os_engine_new() {
  return new (std::nothrow) rpg_os::RulesetEngine();
}

/// Destroys an engine instance created by @ref rpg_os_engine_new.
RPG_OS_WASM_EXPORT void rpg_os_engine_free(void *engine) {
  delete static_cast<rpg_os::RulesetEngine *>(engine);
}

/// Loads a ruleset from an in-memory JSON string. Returns 1 on success, 0 on
/// failure (see @ref rpg_os_engine_last_error).
RPG_OS_WASM_EXPORT int rpg_os_engine_load_json(void *engine, const char *json, int len) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || json == nullptr) {
    return 0;
  }
  return eng->loadRulesetFromJson(std::string(json, static_cast<std::size_t>(len))) ? 1 : 0;
}

/// The last error message (empty string when there is none).
RPG_OS_WASM_EXPORT const char *rpg_os_engine_last_error(void *engine) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  return eng == nullptr ? "" : buffered(eng->lastError());
}

/// The ruleset metadata (attributes, skills, derived stats, resource pools)
/// as JSON — what a character entry form is generated from.
RPG_OS_WASM_EXPORT const char *rpg_os_engine_meta(void *engine) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || !eng->loaded()) {
    return buffered("{}");
  }
  const rpg_os::Ruleset &ruleset = eng->ruleset();
  rpg_os::Json meta = rpg_os::Json::object();
  meta["id"] = ruleset.id;
  meta["name"] = ruleset.name;
  meta["spell_resource"] = ruleset.spellResource;
  // The ruleset's own licensing metadata: the rules are NOT Apache-2.0 — each
  // ruleset is governed by its own licence, optionally linked via
  // licence_source to where the rights holder states it, with the verbatim
  // licence_notice/attribution text the licence requires (e.g. ORC Notice).
  meta["source"] = ruleset.source;
  meta["licence"] = ruleset.licence;
  meta["licence_source"] = ruleset.licenceSource;
  meta["licence_notice"] = ruleset.licenceNotice;
  meta["attribution"] = ruleset.attribution;
  rpg_os::Json attributes = rpg_os::Json::array();
  for (const rpg_os::AttributeDef &def : ruleset.attributes) {
    attributes.push_back({{"id", def.id},
                          {"name", def.name},
                          {"min", def.minValue},
                          {"max", def.maxValue},
                          {"default", def.defaultValue}});
  }
  meta["attributes"] = attributes;
  rpg_os::Json skills = rpg_os::Json::array();
  for (const rpg_os::SkillDef &def : ruleset.skills) {
    rpg_os::Json skill = {{"id", def.id}, {"name", def.name}, {"default", def.defaultValue}};
    rpg_os::Json linked = rpg_os::Json::array();
    for (const std::string &stat : def.attributes) {
      linked.push_back(stat);
    }
    skill["attributes"] = linked;
    skills.push_back(skill);
  }
  meta["skills"] = skills;
  rpg_os::Json derived = rpg_os::Json::array();
  for (const rpg_os::DerivedStatDef &def : ruleset.derivedStats) {
    derived.push_back({{"id", def.id}, {"name", def.name}, {"formula", def.formulaText}});
  }
  meta["derived_stats"] = derived;
  rpg_os::Json pools = rpg_os::Json::array();
  for (const rpg_os::ResourcePoolDef &def : ruleset.resourcePools) {
    pools.push_back({{"id", def.id},
                     {"name", def.name},
                     {"max_stat", def.maxStat},
                     {"min", def.minValue}});
  }
  meta["resource_pools"] = pools;
  return buffered(meta.dump());
}

/// The ruleset's named combatants (archetypes and bestiary entries) as JSON,
/// for pickers/leaderboards: `[{"id","name","kind"}, ...]`.
RPG_OS_WASM_EXPORT const char *rpg_os_engine_entries(void *engine) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || !eng->loaded()) {
    return buffered("[]");
  }
  const rpg_os::Ruleset &ruleset = eng->ruleset();
  rpg_os::Json entries = rpg_os::Json::array();
  if (ruleset.data.is_object()) {
    for (const char *section : {"archetypes", "creatures"}) {
      if (ruleset.data.contains(section) && ruleset.data.at(section).is_array()) {
        for (const rpg_os::Json &record : ruleset.data.at(section)) {
          const std::string id = record.value("id", "");
          entries.push_back({{"id", id},
                             {"name", record.value("name", id)},
                             {"kind", section}});
        }
      }
    }
  }
  return buffered(entries.dump());
}

// ---- Entity ----------------------------------------------------------------

/// Creates an entity from a named archetype or bestiary entry (tries
/// archetypes first). Returns a handle, or nullptr when `id` is unknown.
RPG_OS_WASM_EXPORT void *rpg_os_entity_create(void *engine, const char *id, int variance,
                                              unsigned int seed) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || id == nullptr) {
    return nullptr;
  }
  rpg_os::DefaultRandom rng(seed);
  std::shared_ptr<rpg_os::DynamicEntity> entity =
      eng->createEntityWith(id, toVariance(variance), rng);
  if (entity == nullptr) {
    entity = eng->createCreatureWith(id, toVariance(variance), rng);
  }
  if (entity == nullptr) {
    return nullptr;
  }
  return new (std::nothrow) rpg_os::DynamicEntity(*entity);
}

/// Creates an entity from an arbitrary character sheet (the `DynamicEntity`
/// save form: `{"stats":{...}, "resources":{...}, ...}`), so a character that
/// has no ruleset entry — e.g. one entered into a form — is fully supported.
/// Resource pools are (re)created from the sheet's stats.
RPG_OS_WASM_EXPORT void *rpg_os_entity_create_sheet(void *engine, const char *id,
                                                    const char *sheet, int len) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || !eng->loaded() || id == nullptr) {
    return nullptr;
  }
  auto *entity = new (std::nothrow) rpg_os::DynamicEntity(eng->ruleset(), std::string(id));
  if (entity == nullptr) {
    return nullptr;
  }
  entity->fromJson(parseJson(sheet, len));
  entity->refreshResources();
  return entity;
}

/// Destroys an entity handle.
RPG_OS_WASM_EXPORT void rpg_os_entity_free(void *entity) {
  delete static_cast<rpg_os::DynamicEntity *>(entity);
}

/// Sets a base attribute or skill rating. Returns 1.
RPG_OS_WASM_EXPORT int rpg_os_entity_set_stat(void *entity, const char *stat, int32_t value) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  if (ent == nullptr || stat == nullptr) {
    return 0;
  }
  ent->setBaseAttribute(stat, value);
  return 1;
}

/// Reads a stat (attribute, skill rating, or derived stat). 0 when unknown.
RPG_OS_WASM_EXPORT int32_t rpg_os_entity_get_stat(void *entity, const char *stat) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  return (ent == nullptr || stat == nullptr) ? 0 : ent->getStat(stat);
}

/// Applies `delta` to a resource pool (clamped); returns the amount applied.
RPG_OS_WASM_EXPORT int32_t rpg_os_entity_modify_resource(void *entity, const char *pool,
                                                         int32_t delta) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  return (ent == nullptr || pool == nullptr) ? 0 : ent->modifyResource(pool, delta);
}

/// Current value of a resource pool (e.g. "LP", "HP", "AE"); 0 when absent.
RPG_OS_WASM_EXPORT int32_t rpg_os_entity_get_resource(void *entity, const char *pool) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  return (ent == nullptr || pool == nullptr) ? 0 : ent->resource(pool);
}

/// (Re)creates the entity's resource pools from the current stats.
RPG_OS_WASM_EXPORT void rpg_os_entity_refresh_resources(void *entity) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  if (ent != nullptr) {
    ent->refreshResources();
  }
}

/// Serializes the entity (the `DynamicEntity` save form) as JSON.
RPG_OS_WASM_EXPORT const char *rpg_os_entity_to_json(void *entity) {
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  if (ent == nullptr) {
    return buffered("{}");
  }
  rpg_os::Json out;
  ent->toJson(out);
  return buffered(out.dump());
}

// ---- Checks ----------------------------------------------------------------

/// Resolves a named check for `actor` against `target` (may be null for a
/// solo check). `advantage`: +1 advantage, -1 disadvantage, 0 neutral.
/// Returns the result as JSON.
RPG_OS_WASM_EXPORT const char *rpg_os_check(void *engine, const char *check_type, void *actor,
                                            void *target, int advantage) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  auto *actorEntity = static_cast<rpg_os::DynamicEntity *>(actor);
  auto *targetEntity = static_cast<rpg_os::DynamicEntity *>(target);
  if (eng == nullptr || check_type == nullptr || actorEntity == nullptr) {
    return buffered("{\"error\":\"bad arguments\"}");
  }
  rpg_os::CheckParams params;
  params.advantage = advantage > 0   ? rpg_os::AdvantageMode::Advantage
                     : advantage < 0 ? rpg_os::AdvantageMode::Disadvantage
                                     : rpg_os::AdvantageMode::None;
  rpg_os::DefaultRandom rng;
  const rpg_os::CheckResult result =
      eng->executeCheckEffective(check_type, *actorEntity, targetEntity, params, rng);
  rpg_os::Json out = rpg_os::Json::object();
  out["is_success"] = result.isSuccess;
  out["is_critical_success"] = result.isCriticalSuccess;
  out["is_critical_failure"] = result.isCriticalFailure;
  out["margin"] = result.marginOfSuccess;
  out["remaining_pool"] = result.remainingPool;
  out["quality_level"] = result.qualityLevel;
  rpg_os::Json dice = rpg_os::Json::array();
  for (const int32_t die : result.rawDiceRolls) {
    dice.push_back(die);
  }
  out["raw_dice"] = dice;
  return buffered(out.dump());
}

// ---- Combat ----------------------------------------------------------------

/// Builds a combatant spec from a named archetype or bestiary entry.
/// Returns a handle, or nullptr when `id` is unknown.
RPG_OS_WASM_EXPORT void *rpg_os_spec_from_id(void *engine, const char *id, const char *weapon) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  if (eng == nullptr || id == nullptr) {
    return nullptr;
  }
  auto *spec = new (std::nothrow) rpg_os::CombatantSpec();
  if (spec == nullptr) {
    return nullptr;
  }
  const std::string weaponStr = weapon == nullptr ? "1d6+4" : std::string(weapon);
  if (!rpg_os::makeCombatantSpec(*eng, id, *spec, weaponStr)) {
    delete spec;
    return nullptr;
  }
  return spec;
}

/// Builds a combatant spec from an arbitrary character sheet (entity handle),
/// so any character — even one with no ruleset entry — can fight.
RPG_OS_WASM_EXPORT void *rpg_os_spec_from_entity(void *engine, void *entity, const char *weapon) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  auto *ent = static_cast<rpg_os::DynamicEntity *>(entity);
  if (eng == nullptr || ent == nullptr) {
    return nullptr;
  }
  auto *spec = new (std::nothrow) rpg_os::CombatantSpec();
  if (spec == nullptr) {
    return nullptr;
  }
  const std::string weaponStr = weapon == nullptr ? "1d6+4" : std::string(weapon);
  if (!rpg_os::makeCombatantSpecFromEntity(*eng, *ent, *spec, weaponStr)) {
    delete spec;
    return nullptr;
  }
  return spec;
}

/// Destroys a combatant spec handle.
RPG_OS_WASM_EXPORT void rpg_os_spec_free(void *spec) {
  delete static_cast<rpg_os::CombatantSpec *>(spec);
}

/// Runs one fight between two combatant specs. `seed` makes the run
/// reproducible. Returns the outcome as JSON:
/// `{"winner_index","rounds","max_lp","remaining_lp","a","b"}`.
RPG_OS_WASM_EXPORT const char *rpg_os_fight(void *engine, void *spec_a, void *spec_b,
                                            int max_rounds, unsigned int seed, int use_magic) {
  auto *eng = static_cast<rpg_os::RulesetEngine *>(engine);
  auto *a = static_cast<rpg_os::CombatantSpec *>(spec_a);
  auto *b = static_cast<rpg_os::CombatantSpec *>(spec_b);
  if (eng == nullptr || a == nullptr || b == nullptr || max_rounds <= 0) {
    return buffered("{\"error\":\"bad arguments\"}");
  }
  const std::string checkType = rpg_os::resolveAttackCheckType(eng->ruleset());
  const std::string hpPool = rpg_os::resolveHitPointPool(eng->ruleset());
  if (checkType.empty() || hpPool.empty()) {
    return buffered("{\"error\":\"ruleset has no combat check or hit-point pool\"}");
  }
  rpg_os::DefaultRandom rng(seed);
  const rpg_os::FightOutcome outcome =
      rpg_os::runFight(*eng, *a, *b, checkType, hpPool, max_rounds, rng, use_magic != 0);
  rpg_os::Json out = rpg_os::Json::object();
  out["winner_index"] = outcome.winnerIndex;
  out["rounds"] = outcome.rounds;
  out["max_lp"] = {outcome.maxLp[0], outcome.maxLp[1]};
  out["remaining_lp"] = {outcome.remainingLp[0], outcome.remainingLp[1]};
  out["a"] = a->name;
  out["b"] = b->name;
  return buffered(out.dump());
}

} // extern "C"
