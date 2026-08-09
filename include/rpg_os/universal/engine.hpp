// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file engine.hpp
 * @brief RulesetEngine facade (universal mode).
 *
 * The public entry point for applications using the dynamic engine: load a
 * ruleset, create entities from archetypes, calculate stats, resolve named
 * checks, apply damage through the JSON-driven event pipeline, and register
 * event listeners.
 *
 * @par Why a single facade?
 * An application that wants to "just run a ruleset" should not have to wire
 * the loader, the resolver, the entity factory, and the event bus together
 * by hand. The engine owns that wiring: it holds the loaded @c Ruleset, the
 * @c EventBus, and the load state, and exposes one cohesive API. The
 * individual pieces remain separately usable (the code generator and tests
 * use them directly), but the facade is the recommended entry point.
 */
#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <rpg_os/common/event_system.hpp>
#include <rpg_os/common/json.hpp>
#include <rpg_os/common/types.hpp>
#include <rpg_os/core/checks.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <rpg_os/core/math.hpp>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/check_resolver.hpp>
#include <rpg_os/universal/dynamic_entity.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace rpg_os {

/// The universal (dynamic) engine facade.
class RulesetEngine {
public:
  /// Loads a ruleset from a JSON string. Returns false (and records a message
  /// in `lastError()`) on parse or validation failure.
  ///
  /// @par Why swallow exceptions here but not in the loader?
  /// A facade is friendlier to application code when "did the load succeed?"
  /// is a boolean plus a message rather than a try/catch. The loader itself
  /// still throws precise errors for callers that want them; the engine
  /// converts them into @ref lastError for the common case.
  bool loadRulesetFromJson(std::string_view jsonContent) {
    try {
      m_ruleset = RulesetLoader::loadFromString(jsonContent);
      m_loaded = true;
      m_lastError.clear();
      return true;
    } catch (const std::exception &e) {
      m_lastError = e.what();
      m_loaded = false;
      return false;
    }
  }

  /// Loads a ruleset from a file (UTF-8). Returns false on I/O or validation
  /// failure; see `lastError()`.
  bool loadRulesetFromFile(std::string_view path) {
    const std::string pathStr(path);
    std::ifstream file(pathStr);
    if (!file) {
      m_lastError = "cannot open file '" + pathStr + "'";
      m_loaded = false;
      return false;
    }
    // Read via rdbuf() rather than the istreambuf_iterator range idiom: the
    // iterator path trips a -Wnull-dereference false positive in libstdc++ 13
    // under -Werror, and streaming the streambuf is the idiomatic whole-file
    // read anyway.
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadRulesetFromJson(buffer.str());
  }

  /// Re-runs validation on the loaded ruleset; false when no ruleset is
  /// loaded or validation fails (loading already validates). Useful after a
  /// ruleset has been mutated in place.
  [[nodiscard]] bool validateRuleset() const {
    if (!m_loaded) {
      return false;
    }
    try {
      RulesetLoader::validate(m_ruleset);
      return true;
    } catch (const std::exception &) {
      return false;
    }
  }

  /// Creates an entity from the named archetype in the ruleset's `data`
  /// section (ranged values picked at random), or nullptr when the archetype
  /// (or ruleset) does not exist.
  [[nodiscard]] std::shared_ptr<DynamicEntity> createEntity(std::string_view archetypeId) {
    return createEntity(archetypeId, Variance::Random);
  }

  /// Creates an entity from the named archetype, picking ranged values
  /// according to `variance` (weakest / weak / average / strong / strongest).
  [[nodiscard]] std::shared_ptr<DynamicEntity> createEntity(std::string_view archetypeId,
                                                            Variance variance) {
    DefaultRandom rng;
    return createEntityWith(archetypeId, variance, rng);
  }

  /// Creates an entity from the named archetype with the caller-supplied RNG
  /// (deterministic tests inject a scripted RNG, Monte-Carlo runs inject a
  /// seeded one).
  template <RandomNumberGenerator Rng>
  [[nodiscard]] std::shared_ptr<DynamicEntity> createEntityWith(std::string_view archetypeId,
                                                                Variance variance, Rng &rng) {
    if (!m_loaded || !m_ruleset.data.is_object() || !m_ruleset.data.contains("archetypes")) {
      return nullptr;
    }
    for (const Json &archetype : m_ruleset.data.at("archetypes")) {
      if (archetype.value("id", "") == archetypeId) {
        auto entity = std::make_shared<DynamicEntity>(m_ruleset, std::string(archetypeId));
        entity->loadFromArchetype(archetype, variance, rng);
        return entity;
      }
    }
    return nullptr;
  }

  /// Creates a creature from the named entry in the ruleset's `data.creatures`
  /// section, or nullptr when it does not exist. Bestiary entries may carry
  /// ranged values (e.g. hit points as "2d6"), so a variance can be requested.
  ///
  /// @par Why a separate creation path for creatures?
  /// Archetypes and bestiary entries are authored differently in the source
  /// material (a PC block vs. a monster stat line), and callers think of them
  /// as distinct pools. Keeping both lookup paths explicit lets an application
  /// ask "the goblin" without ambiguity about which section it came from.
  [[nodiscard]] std::shared_ptr<DynamicEntity>
  createCreature(std::string_view creatureId, Variance variance = Variance::Random) {
    DefaultRandom rng;
    return createCreatureWith(creatureId, variance, rng);
  }

  /// Creates a creature from `data.creatures` with the caller-supplied RNG.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] std::shared_ptr<DynamicEntity> createCreatureWith(std::string_view creatureId,
                                                                  Variance variance, Rng &rng) {
    if (!m_loaded || !m_ruleset.data.is_object() || !m_ruleset.data.contains("creatures")) {
      return nullptr;
    }
    for (const Json &creature : m_ruleset.data.at("creatures")) {
      if (creature.value("id", "") == creatureId) {
        auto entity = std::make_shared<DynamicEntity>(m_ruleset, std::string(creatureId));
        entity->loadFromArchetype(creature, variance, rng);
        return entity;
      }
    }
    return nullptr;
  }

  /// Calculates a stat (attribute, skill rating, or derived stat) for `entity`.
  /// This is a thin passthrough to @ref DynamicEntity::getStat that gives the
  /// facade a uniform "ask the engine for a value" API.
  [[nodiscard]] int32_t calculateStat(const DynamicEntity &entity, std::string_view statId) const {
    return entity.getStat(statId);
  }

  /// Resolves a named check type from the ruleset using a caller-supplied RNG
  /// (deterministic tests inject a scripted RNG).
  ///
  /// @par Why is the target a pointer?
  /// Checks may or may not have a target (a D&D ability check vs. a monster,
  /// or a solo DSA talent check). A `nullptr` target is the "no target" case
  /// and is resolved against @ref NullStatProvider, keeping the common solo
  /// case ergonomic.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] CheckResult executeCheck(std::string_view checkTypeId, const DynamicEntity &actor,
                                         const DynamicEntity *target, const CheckParams &params,
                                         Rng &rng) const {
    if (target != nullptr) {
      return CheckResolver::resolve(m_ruleset, actor, *target, checkTypeId, params, rng);
    }
    return CheckResolver::resolve(m_ruleset, actor, NullStatProvider{}, checkTypeId, params, rng);
  }

  /// Resolves a named check type using a fresh default RNG. Convenience for
  /// application code that does not care about reproducibility.
  [[nodiscard]] CheckResult executeCheck(std::string_view checkTypeId, const DynamicEntity &actor,
                                         const DynamicEntity *target,
                                         const CheckParams &params) const {
    DefaultRandom rng;
    return executeCheck(checkTypeId, actor, target, params, rng);
  }

  /// Resolves a skill check for a named skill, using the skill's own linked
  /// attributes and the skill rating as the pool (real TDE model, where every
  /// talent has its own three attributes). Skill checks have no target. Throws
  /// std::invalid_argument for an unknown skill or a skill without three linked
  /// attributes.
  ///
  /// @par Why bypass the check-type mechanism here?
  /// A talent check *is* a 3d20 pool check whose attributes live on the skill
  /// definition, not in a named check type. Driving it straight from the
  /// skill keeps the ruleset from having to duplicate each talent as a check
  /// type — one source of truth for a talent's attributes.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] CheckResult executeSkillCheck(std::string_view skillId, const DynamicEntity &actor,
                                              const CheckParams &params, Rng &rng) const {
    const SkillDef *skill = m_ruleset.findSkill(skillId);
    if (skill == nullptr) {
      throw std::invalid_argument("unknown skill '" + std::string(skillId) + "'");
    }
    if (skill->attributes.size() != 3) {
      throw std::invalid_argument("skill '" + std::string(skillId) +
                                  "' is not a three-attribute talent");
    }
    return resolveTripleRollUnderPool(actor, skill->attributes[0], skill->attributes[1],
                                      skill->attributes[2], skillId, params, rng);
  }

  /// Resolves a skill check using a fresh default RNG. Convenience overload.
  [[nodiscard]] CheckResult executeSkillCheck(std::string_view skillId, const DynamicEntity &actor,
                                              const CheckParams &params) const {
    DefaultRandom rng;
    return executeSkillCheck(skillId, actor, params, rng);
  }

  /// Applies `rawDamage` to `target`'s resource `resourceId` through the
  /// event pipeline:
  ///   1. `OnDamageCalculated` rule triggers may reduce the damage (e.g. DSA
  ///      armor rating) by modifying `event.damage`;
  ///   2. the final damage is applied to the resource;
  ///   3. `OnDamageTaken` rule triggers run (e.g. wound checks).
  /// Returns the damage actually applied.
  ///
  /// @par Why route damage through events even when no triggers exist?
  /// It is cheaper to always run the pipeline than to ask "are there any
  /// relevant triggers?" first — with no triggers registered the loop is a
  /// no-op — and it guarantees ruleset-defined reactions (armor absorption,
  /// wound checks) fire uniformly for universal and specific mode alike.
  int32_t applyDamage(DynamicEntity &actor, DynamicEntity &target, std::string_view resourceId,
                      int32_t rawDamage, const Json &env = {}) {
    EventData data;
    data.payload = {{"damage", rawDamage},
                    {"raw_damage", rawDamage},
                    {"attacker_id", actor.id()},
                    {"target_id", target.id()}};
    fireEvent(EventType::OnDamageCalculated, data, actor, &target, env);
    int32_t finalDamage = data.getInt("damage", rawDamage);
    if (finalDamage < 0) {
      finalDamage = 0;
    }
    const int32_t applied = target.modifyResource(resourceId, -finalDamage);
    data.payload["applied_damage"] = -applied;
    fireEvent(EventType::OnDamageTaken, data, actor, &target, env);
    return applied;
  }

  /// Registers a user-facing event listener (returns a handle for removal).
  /// User listeners run *after* the ruleset's own JSON-driven triggers, so
  /// they observe the final, mutated payload.
  [[nodiscard]] uint64_t registerEventListener(EventType type, EventBus::Callback callback) {
    return m_eventBus.addListener(type, std::move(callback));
  }

  /// Removes a previously registered event listener.
  bool unregisterEventListener(uint64_t listenerId) {
    return m_eventBus.removeListener(listenerId);
  }

  /// The loaded ruleset (only valid when `loaded()` is true).
  [[nodiscard]] const Ruleset &ruleset() const noexcept {
    return m_ruleset;
  }

  /// Whether a ruleset has been loaded successfully.
  [[nodiscard]] bool loaded() const noexcept {
    return m_loaded;
  }

  /// Message from the last failed load / validation. Empty after a successful
  /// load.
  [[nodiscard]] const std::string &lastError() const noexcept {
    return m_lastError;
  }

private:
  /// Runs the ruleset's JSON-driven triggers for `type` (mutating `data`), then
  /// notifies user-facing listeners. The ordering is deliberate: ruleset rules
  /// (e.g. armor absorption) must see the payload first and mutate it, so
  /// user callbacks observe the final state — matching how a tabletop GM would
  /// apply the book rule before announcing the outcome.
  void fireEvent(EventType type, EventData &data, DynamicEntity &actor, DynamicEntity *target,
                 const Json &env) {
    runRuleTriggers(type, data, actor, target, env);
    m_eventBus.dispatch(type, data);
  }

  /// Executes the ruleset's event triggers of `type` against the payload.
  /// Each trigger may carry a condition; only triggers whose condition
  /// evaluates non-zero actually fire their actions.
  void runRuleTriggers(EventType type, EventData &data, DynamicEntity &actor, DynamicEntity *target,
                       const Json &env) {
    for (const EventTriggerDef &trigger : m_ruleset.eventTriggers) {
      if (trigger.trigger != type) {
        continue;
      }
      if (!trigger.conditionText.empty()) {
        const EntityContext context(actor, target, env, data.payload);
        if (trigger.condition.evaluate(context) == 0.0) {
          continue;
        }
      }
      for (const EventActionDef &action : trigger.actions) {
        executeAction(action, data, actor, target, env);
      }
    }
  }

  /// Executes a single event action. Modifies `data` for
  /// `modify_event_damage`; otherwise mutates the actor (when no target) or
  /// the target.
  ///
  /// @par Why target-if-present-else-actor for resource/condition actions?
  /// "Consume a resource" and "apply a condition" act on whoever was hit when
  /// a target exists, and on the acting character for solo effects. That
  /// single rule keeps action semantics uniform across trigger sites.
  void executeAction(const EventActionDef &action, EventData &data, DynamicEntity &actor,
                     DynamicEntity *target, const Json &env) {
    const EntityContext context(actor, target, env, data.payload);
    DynamicEntity &subject = target != nullptr ? *target : actor;
    if (action.type == "modify_event_damage") {
      const double value = action.formula.evaluate(context);
      data.payload["damage"] = value;
    } else if (action.type == "consume_resource") {
      (void)subject.modifyResource(action.resource, -action.amount);
    } else if (action.type == "apply_condition") {
      const int32_t stacks =
          action.stacksText.empty() ? 1 : math::toStat(action.stacks.evaluate(context));
      subject.addCondition(action.condition, stacks);
    }
  }

  Ruleset m_ruleset;
  bool m_loaded{false};
  std::string m_lastError;
  EventBus m_eventBus;
};

} // namespace rpg_os
