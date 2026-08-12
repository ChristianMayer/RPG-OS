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
  /// attributes and the skill rating as the pool (the generic pool
  /// resolution applied to the skill's three linked attributes). Skill checks
  /// have no target. Throws std::invalid_argument for an unknown skill or a
  /// skill without exactly three linked attributes.
  ///
  /// @par Why drive it from the skill definition rather than a check type?
  /// A skill check's attributes live on the skill definition, not in a named
  /// check type. Building a generic pool recipe from those attributes keeps
  /// the skill as the single source of truth and requires no ruleset-specific
  /// code — the pool resolution (and its double-roll criticals and quality
  /// grading) is entirely described by the recipe.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] CheckResult executeSkillCheck(std::string_view skillId, const DynamicEntity &actor,
                                              const CheckParams &params, Rng &rng) const {
    const SkillDef *skill = m_ruleset.findSkill(skillId);
    if (skill == nullptr) {
      throw std::invalid_argument("unknown skill '" + std::string(skillId) + "'");
    }
    if (skill->attributes.size() != 3) {
      throw std::invalid_argument("skill '" + std::string(skillId) +
                                  "' is not a three-attribute skill check");
    }
    CheckRecipe recipe;
    recipe.resolution = Resolution::Pool;
    recipe.dice = "3d20"_dice;
    recipe.numPoolAttributes = 3;
    recipe.poolAttributes = {skill->attributes[0], skill->attributes[1], skill->attributes[2]};
    recipe.poolStat = std::string(skillId);
    recipe.criticalStyle = CriticalStyle::DoubleRoll;
    recipe.fumbleStyle = CriticalStyle::DoubleRoll;
    recipe.grading = Grading::PoolQuality;
    recipe.difficultyMode = DifficultyMode::ToStat;
    return resolveCheck(actor, NullStatProvider{}, recipe, params, rng);
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

  /// Looks up a raw data record by id in a named `data` section (e.g.
  /// "spells", "poisons", "diseases", "conditions", "items", "archetypes",
  /// "creatures"). Returns nullptr when the section or record does not exist.
  ///
  /// @par Why raw JSON?
  /// The `data` database is intentionally free-form — each ruleset's records
  /// carry exactly the fields its source book has. The engine exposes every
  /// record verbatim so an application can read anything the ruleset covers
  /// (magic, illness, equipment, ...) without the engine having to model it.
  [[nodiscard]] const Json *findDataRecord(std::string_view section, std::string_view id) const {
    if (!m_ruleset.data.is_object()) {
      return nullptr;
    }
    const auto it = m_ruleset.data.find(std::string(section));
    if (it == m_ruleset.data.end() || !it->is_array()) {
      return nullptr;
    }
    for (const Json &record : *it) {
      if (record.value("id", "") == id) {
        return &record;
      }
    }
    return nullptr;
  }

  /// The named data record accessors for the common sections.
  [[nodiscard]] const Json *findSpell(std::string_view id) const {
    return findDataRecord("spells", id);
  }
  [[nodiscard]] const Json *findItem(std::string_view id) const {
    return findDataRecord("items", id);
  }
  [[nodiscard]] const Json *findCondition(std::string_view id) const {
    return findDataRecord("conditions", id);
  }
  [[nodiscard]] const Json *findPoison(std::string_view id) const {
    return findDataRecord("poisons", id);
  }
  [[nodiscard]] const Json *findDisease(std::string_view id) const {
    return findDataRecord("diseases", id);
  }
  [[nodiscard]] const Json *findArchetype(std::string_view id) const {
    return findDataRecord("archetypes", id);
  }
  [[nodiscard]] const Json *findCreature(std::string_view id) const {
    return findDataRecord("creatures", id);
  }

  /// The outcome of casting a spell through @ref castSpell.
  struct SpellResult {
    bool cast{false};         ///< the spell was cast (cost paid; declared check passed)
    CheckResult check;        ///< the casting check, when the spell declares one
    int32_t cost{0};          ///< resource points spent
    int32_t appliedDamage{0}; ///< damage applied to the target (when the spell deals damage)
    std::string resourceId;   ///< the resource pool the cost was drawn from
  };

  /// The outcome of applying an affliction through @ref applyAffliction.
  struct AfflictionResult {
    bool resisted{false};      ///< a declared save was passed
    bool saveRolled{false};    ///< a save check was rolled
    int32_t effectsApplied{0}; ///< how many structured effects were applied
  };

  /// Casts a spell from the ruleset's `data.spells` (see @ref SpellResult).
  ///
  /// The casting is fully data-driven: the cost is read from the spell record
  /// (`cost` / `ae_cost`, or `level` — one point per level), drawn from the
  /// ruleset's declared `spell_resource` (or the caller-supplied resource),
  /// and the spell's `check` field (a named check type or an "A/B/C"
  /// attribute list) is resolved when present. A spell with `damage` applies
  /// the rolled damage to the target's primary hit-point pool through the
  /// event pipeline. Throws std::invalid_argument for an unknown spell.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] SpellResult castSpell(std::string_view spellId, DynamicEntity &actor,
                                      DynamicEntity *target, const CheckParams &params, Rng &rng) {
    return castSpell(spellId, actor, target, spellResourceId(), params, rng);
  }

  /// As above, but draws the cost from `resourceId` instead of the ruleset's
  /// declared spell resource.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] SpellResult castSpell(std::string_view spellId, DynamicEntity &actor,
                                      DynamicEntity *target, std::string_view resourceId,
                                      const CheckParams &params, Rng &rng) {
    SpellResult result;
    const Json *spell = findSpell(spellId);
    if (spell == nullptr) {
      throw std::invalid_argument("unknown spell '" + std::string(spellId) + "'");
    }
    // Cost: `cost` or `ae_cost`, else `level` (one point per level).
    if (spell->contains("cost") && spell->at("cost").is_number_integer()) {
      result.cost = spell->at("cost").get<int32_t>();
    } else if (spell->contains("ae_cost") && spell->at("ae_cost").is_number_integer()) {
      result.cost = spell->at("ae_cost").get<int32_t>();
    } else if (spell->contains("level") && spell->at("level").is_number_integer()) {
      result.cost = spell->at("level").get<int32_t>();
    }
    result.resourceId = std::string(resourceId);
    if (result.cost > 0 && !resourceId.empty()) {
      if (actor.resource(resourceId) < result.cost) {
        result.cast = false; // cannot afford the spell
        return result;
      }
      (void)actor.modifyResource(resourceId, -result.cost);
    }

    // Casting check (named check type, or an "A/B/C" attribute list).
    if (spell->contains("check") && spell->at("check").is_string()) {
      const std::string checkText = spell->at("check").get<std::string>();
      if (m_ruleset.findCheckType(checkText) != nullptr) {
        const NullStatProvider noTarget;
        result.check =
            target != nullptr
                ? CheckResolver::resolve(m_ruleset, actor, *target, checkText, params, rng)
                : CheckResolver::resolve(m_ruleset, actor, noTarget, checkText, params, rng);
      } else {
        result.check = resolveSpellCheck(actor, checkText, params, rng);
      }
      result.cast = result.check.isSuccess;
    } else {
      result.cast = true;
    }

    // Damage: roll the spell's damage and apply it to the target's hit points.
    // `applyDamage` reports the (negative) pool delta, so negate it into the
    // positive "damage dealt" the caller expects.
    if (target != nullptr && spell->contains("damage")) {
      const int32_t damage = readVariantValue(spell->at("damage"), Variance::Random, rng);
      const std::string hitPool = resolveHitPointPoolId();
      if (!hitPool.empty()) {
        result.appliedDamage = -applyDamage(actor, *target, hitPool, damage);
      }
    }
    return result;
  }

  /// Casts a spell using a fresh default RNG (convenience overload).
  [[nodiscard]] SpellResult castSpell(std::string_view spellId, DynamicEntity &actor,
                                      DynamicEntity *target, const CheckParams &params) {
    DefaultRandom rng;
    return castSpell(spellId, actor, target, params, rng);
  }

  /// Applies an affliction (a poison or disease) from `data.<section>` to
  /// `victim` (see @ref AfflictionResult).
  ///
  /// The application is fully data-driven: if the record declares a `save`
  /// stat, the victim rolls a generic roll-under check against it and the
  /// affliction is resisted on success. Otherwise (or on a failed save) every
  /// structured `effects[]` entry is applied: a resource pool id damages that
  /// pool, a condition id (or the effect's `condition` field) applies stacks,
  /// and any other stat id is reduced by `amount`. Prose-only effects are left
  /// for the caller, who can read the full record via @ref findPoison /
  /// @ref findDisease. Throws std::invalid_argument for an unknown record.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] AfflictionResult applyAffliction(std::string_view section, std::string_view id,
                                                 DynamicEntity &victim, const CheckParams &params,
                                                 Rng &rng) {
    AfflictionResult result;
    const Json *affliction = findDataRecord(section, id);
    if (affliction == nullptr) {
      throw std::invalid_argument("unknown " + std::string(section) + " '" + std::string(id) + "'");
    }
    if (affliction->contains("save") && affliction->at("save").is_string()) {
      const std::string save = affliction->at("save").get<std::string>();
      if (m_ruleset.hasStat(save)) {
        result.saveRolled = true;
        CheckRecipe recipe;
        recipe.resolution = Resolution::Threshold;
        recipe.dice = "1d20"_dice;
        recipe.comparison = Comparison::LessEqual;
        recipe.thresholdSource = ThresholdSource::ActorStat;
        recipe.thresholdStat = save;
        recipe.difficultyMode = DifficultyMode::ToStat;
        const CheckResult saveResult =
            resolveCheck(victim, NullStatProvider{}, recipe, params, rng);
        if (saveResult.isSuccess) {
          result.resisted = true;
          return result;
        }
      }
    }
    if (affliction->contains("effects") && affliction->at("effects").is_array()) {
      for (const Json &effect : affliction->at("effects")) {
        const std::string stat = effect.value("stat", "");
        if (stat.empty()) {
          continue;
        }
        int32_t amount = 1;
        if (effect.contains("amount")) {
          amount = readVariantValue(effect.at("amount"), Variance::Random, rng);
        }
        if (m_ruleset.isResourcePool(stat)) {
          (void)victim.modifyResource(stat, -amount);
          ++result.effectsApplied;
        } else if (effect.contains("condition") && effect.at("condition").is_string()) {
          victim.addCondition(effect.at("condition").get<std::string>(), amount);
          ++result.effectsApplied;
        } else if (m_ruleset.isCondition(stat)) {
          victim.addCondition(stat, amount);
          ++result.effectsApplied;
        } else if (m_ruleset.findAttribute(stat) != nullptr) {
          victim.setBaseAttribute(stat, victim.baseAttribute(stat) - amount);
          ++result.effectsApplied;
        }
      }
    }
    return result;
  }

  /// Applies an affliction using a fresh default RNG (convenience overload).
  [[nodiscard]] AfflictionResult applyAffliction(std::string_view section, std::string_view id,
                                                 DynamicEntity &victim, const CheckParams &params) {
    DefaultRandom rng;
    return applyAffliction(section, id, victim, params, rng);
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

  /// The ruleset's declared spell resource (empty when none).
  [[nodiscard]] std::string spellResourceId() const {
    return m_ruleset.spellResource;
  }

  /// The id of the ruleset's primary hit-point pool (the first resource pool
  /// with a minimum of 0), e.g. "HP" / "LP". Empty when none exists.
  [[nodiscard]] std::string resolveHitPointPoolId() const {
    for (const ResourcePoolDef &pool : m_ruleset.resourcePools) {
      if (pool.minValue == 0) {
        return pool.id;
      }
    }
    return {};
  }

  /// Resolves a spell's casting check from its raw "A/B/C" attribute list
  /// (e.g. "SGC/SGC/INT", or "COU/INT/CHA (modified by Spirit)" — the
  /// parenthetical suffix and whitespace are ignored). The check is a generic
  /// 3d20 pool roll against the listed attributes with no skill pool.
  template <RandomNumberGenerator Rng>
  [[nodiscard]] CheckResult resolveSpellCheck(const DynamicEntity &actor,
                                              std::string_view checkText, const CheckParams &params,
                                              Rng &rng) const {
    CheckRecipe recipe;
    recipe.resolution = Resolution::Pool;
    recipe.dice = "3d20"_dice;
    recipe.criticalStyle = CriticalStyle::DoubleRoll;
    recipe.fumbleStyle = CriticalStyle::DoubleRoll;
    recipe.grading = Grading::PoolQuality;
    recipe.difficultyMode = DifficultyMode::ToStat;

    const std::size_t paren = checkText.find('(');
    const std::string_view head =
        paren == std::string_view::npos ? checkText : checkText.substr(0, paren);
    std::size_t begin = 0;
    while (begin < head.size()) {
      while (begin < head.size() && (head[begin] == ' ' || head[begin] == '/')) {
        ++begin;
      }
      if (begin >= head.size()) {
        break;
      }
      std::size_t end = begin;
      while (end < head.size() && head[end] != '/' && head[end] != ' ') {
        ++end;
      }
      if (recipe.numPoolAttributes < 3) {
        recipe.poolAttributes[recipe.numPoolAttributes++] =
            std::string(head.substr(begin, end - begin));
      }
      begin = end;
    }
    return resolveCheck(actor, NullStatProvider{}, recipe, params, rng);
  }

  Ruleset m_ruleset;
  bool m_loaded{false};
  std::string m_lastError;
  EventBus m_eventBus;
};

} // namespace rpg_os
