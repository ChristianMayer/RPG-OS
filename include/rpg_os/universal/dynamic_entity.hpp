// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Dynamic entity (universal mode).
//
// Entities hold base attribute values, skill ratings, resource pools, and
// conditions in unordered maps and compute derived stats by evaluating the
// ruleset's formulas with the AST evaluator. It satisfies the shared
// `StatProvider` concept, so the same check algorithms from core/checks.hpp
// work here and on the generated specific-mode characters.
//
// A DynamicEntity keeps a reference to its Ruleset; the ruleset must outlive
// every entity (the RulesetEngine owns both).
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/entity.hpp>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/expression.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rpg_os {

class DynamicEntity;

/// Reads a numeric field from a JSON object; returns false when absent or not
/// numeric.
inline bool readJsonNumber(const Json &obj, std::string_view key, double &out) {
  if (!obj.is_object()) {
    return false;
  }
  const auto it = obj.find(std::string(key));
  if (it == obj.end() || !it->is_number()) {
    return false;
  }
  out = it->get<double>();
  return true;
}

/// EvalContext for universal mode. Resolves:
///   - bare ids and `actor.X`  -> the actor's stats,
///   - `target.X`              -> the (optional) target's stats,
///   - `env.X`                 -> values from an environment bag,
///   - `event.X` / `action.X`  -> values from a parameters bag.
class EntityContext final : public EvalContext {
public:
  EntityContext(const DynamicEntity &actor, const DynamicEntity *target, const Json &env,
                const Json &params)
      : m_actor(actor), m_target(target), m_env(env), m_params(params) {}

  [[nodiscard]] bool resolve(std::string_view path, double &out) const override;

private:
  const DynamicEntity &m_actor;
  const DynamicEntity *m_target;
  const Json &m_env;
  const Json &m_params;
};

/// A universal-mode entity: dynamic stats, resources, and conditions.
class DynamicEntity {
public:
  /// Creates an empty entity bound to `ruleset`.
  DynamicEntity(const Ruleset &ruleset, std::string id)
      : m_ruleset(&ruleset), m_id(std::move(id)) {}

  /// The entity's identifier (e.g. the archetype id).
  [[nodiscard]] const std::string &id() const noexcept {
    return m_id;
  }

  /// StatProvider: returns an attribute, skill, or derived stat value.
  [[nodiscard]] int32_t getStat(std::string_view statId) const {
    const auto it = m_stats.find(std::string(statId));
    if (it != m_stats.end()) {
      return it->second;
    }
    if (m_ruleset->findDerivedStat(statId) != nullptr) {
      return computeDerived(statId);
    }
    return 0;
  }

  /// Sets a base attribute (or skill rating) value.
  void setBaseAttribute(std::string_view attrId, int32_t value) {
    m_stats[std::string(attrId)] = value;
  }

  /// Returns a base attribute (or skill rating) value, or 0 when unset.
  [[nodiscard]] int32_t baseAttribute(std::string_view attrId) const {
    const auto it = m_stats.find(std::string(attrId));
    return it == m_stats.end() ? 0 : it->second;
  }

  /// Whether a base attribute (or skill rating) is set.
  [[nodiscard]] bool hasBaseAttribute(std::string_view attrId) const {
    return m_stats.find(std::string(attrId)) != m_stats.end();
  }

  /// Current value of a resource pool, or 0 when absent.
  [[nodiscard]] int32_t resource(std::string_view resourceId) const {
    const auto it = m_resources.find(std::string(resourceId));
    return it == m_resources.end() ? 0 : it->second.current;
  }

  /// Applies `delta` to a resource pool (clamped to its bounds); returns the
  /// amount actually applied.
  [[nodiscard]] int32_t modifyResource(std::string_view resourceId, int32_t delta) {
    const auto it = m_resources.find(std::string(resourceId));
    return it == m_resources.end() ? 0 : it->second.modify(delta);
  }

  /// Adds `stacks` of a condition (stacks accumulate).
  void addCondition(std::string_view conditionId, int32_t stacks = 1) {
    m_conditions[std::string(conditionId)] += stacks;
  }

  /// Removes a condition entirely.
  void removeCondition(std::string_view conditionId) {
    m_conditions.erase(std::string(conditionId));
  }

  /// Whether the entity currently has the condition.
  [[nodiscard]] bool hasCondition(std::string_view conditionId) const {
    const auto it = m_conditions.find(std::string(conditionId));
    return it != m_conditions.end() && it->second > 0;
  }

  /// Stack count of a condition (0 when absent).
  [[nodiscard]] int32_t conditionStacks(std::string_view conditionId) const {
    const auto it = m_conditions.find(std::string(conditionId));
    return it == m_conditions.end() ? 0 : it->second;
  }

  /// Loads attributes, skills, resources, and conditions from an archetype
  /// JSON record (ranged values picked at random), then (re)initializes the
  /// resource pools.
  void loadFromArchetype(const Json &archetype) {
    DefaultRandom rng;
    loadFromArchetype(archetype, Variance::Random, rng);
  }

  /// As above, but picks ranged values according to `variance` (weakest, weak,
  /// average, strong, strongest, or random) with the caller-supplied RNG.
  template <RandomNumberGenerator Rng>
  void loadFromArchetype(const Json &archetype, Variance variance, Rng &rng) {
    if (archetype.contains("attributes")) {
      for (const auto &[statId, value] : archetype.at("attributes").items()) {
        m_stats[statId] = readVariantValue(value, variance, rng);
      }
    }
    if (archetype.contains("skills")) {
      for (const auto &[skillId, value] : archetype.at("skills").items()) {
        m_stats[skillId] = readVariantValue(value, variance, rng);
      }
    }
    refreshResources();
    if (archetype.contains("resources")) {
      for (const auto &[resourceId, value] : archetype.at("resources").items()) {
        m_resources[resourceId].current = readVariantValue(value, variance, rng);
      }
    }
    if (archetype.contains("conditions")) {
      for (const auto &[conditionId, value] : archetype.at("conditions").items()) {
        m_conditions[conditionId] = value.get<int32_t>();
      }
    }
  }

private:
  /// (Re)creates the resource pools from the ruleset definitions, computing
  /// each maximum from the current stats.
  void refreshResources() {
    for (const ResourcePoolDef &def : m_ruleset->resourcePools) {
      const int32_t maxValue = getStat(def.maxStat);
      const auto it = m_resources.find(def.id);
      if (it == m_resources.end()) {
        m_resources.emplace(def.id, ResourcePool{maxValue, def.minValue, maxValue});
      } else {
        it->second.min = def.minValue;
        it->second.max = maxValue;
      }
    }
  }

  /// Evaluates a derived-stat formula for this entity (no target / env).
  [[nodiscard]] int32_t computeDerived(std::string_view statId) const {
    const DerivedStatDef *def = m_ruleset->findDerivedStat(statId);
    if (def == nullptr) {
      return 0;
    }
    const Json emptyEnv = Json::object();
    const Json emptyParams = Json::object();
    const EntityContext context(*this, nullptr, emptyEnv, emptyParams);
    return math::toStat(def->expression.evaluate(context));
  }

  const Ruleset *m_ruleset;
  std::string m_id;
  std::unordered_map<std::string, int32_t> m_stats;
  std::unordered_map<std::string, ResourcePool> m_resources;
  std::unordered_map<std::string, int32_t> m_conditions;
};

inline bool EntityContext::resolve(std::string_view path, double &out) const {
  const std::size_t dot = path.find('.');
  if (dot == std::string_view::npos) {
    out = static_cast<double>(m_actor.getStat(path));
    return true;
  }
  const std::string_view prefix = path.substr(0, dot);
  const std::string_view rest = path.substr(dot + 1);
  if (prefix == "actor") {
    out = static_cast<double>(m_actor.getStat(rest));
    return true;
  }
  if (prefix == "target") {
    out = m_target != nullptr ? static_cast<double>(m_target->getStat(rest)) : 0.0;
    return true;
  }
  if (prefix == "env") {
    return readJsonNumber(m_env, rest, out);
  }
  if (prefix == "event" || prefix == "action") {
    return readJsonNumber(m_params, rest, out);
  }
  return false;
}

static_assert(StatProvider<DynamicEntity>);

} // namespace rpg_os
