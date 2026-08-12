// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file dynamic_entity.hpp
 * @brief Dynamic entity (universal mode).
 *
 * Entities hold base attribute values, skill ratings, resource pools, and
 * conditions in @c unordered_map and compute derived stats by evaluating the
 * ruleset's formulas with the AST evaluator. It satisfies the shared
 * @c StatProvider concept, so the same check algorithms from
 * @c core/checks.hpp work here and on the generated specific-mode
 * characters.
 *
 * @par Why maps instead of named members?
 * This is the universal mode: the set of attributes, skills, and derived stats
 * is not known at compile time — it comes from whichever ruleset is loaded.
 * A map is the only representation that can hold "any ruleset's" entities
 * without regenerating code. The price (hash lookups, no compile-time stat
 * names) is exactly what the generated specific mode trades away; the two are
 * complementary, and the parity tests pin them to identical behaviour.
 *
 * @par Ownership
 * A @c DynamicEntity keeps a reference to its @c Ruleset; the ruleset must
 * outlive every entity (the @c RulesetEngine owns both).
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/advancement.hpp>
#include <rpg_os/core/effects.hpp>
#include <rpg_os/core/entity.hpp>
#include <rpg_os/core/equipment.hpp>
#include <rpg_os/core/inventory.hpp>
#include <rpg_os/core/modifier.hpp>
#include <rpg_os/core/money.hpp>
#include <rpg_os/core/spellbook.hpp>
#include <rpg_os/core/variance.hpp>
#include <rpg_os/universal/expression.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpg_os {

class DynamicEntity;

/// Reads a numeric field from a JSON object; returns false when absent or not
/// numeric.
///
/// @par Why a helper instead of inline lookups?
/// Environment and parameter bags share the same "maybe a number" lookup
/// semantics. Centralising it keeps the error handling (absent / wrong type =>
/// unresolved) identical for @c env.*, @c event.*, and @c action.* paths, so a
/// formula author never sees one prefix behave differently from another.
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

/// Converts a JSON modifier entry into a @ref Modifier.
///
/// @par Why a JSON->Modifier converter here?
/// Both equipped items (`modifiers[]`) and active conditions
/// (`stat_modifiers[]`) express their effects as JSON so a ruleset author
/// never writes C++. This is the single place that turns those JSON entries
/// into the @c core/modifier.hpp steps, so both sources share one vocabulary:
/// `{"type": "add"|"override"|"multiply"|"clamp", "value"/"factor"/bounds}`.
/// Unknown types degrade to a no-op Add(0) rather than throwing — consistent
/// with how the engine treats unexpected rule fields.
inline Modifier parseModifierJson(const Json &mod) {
  Modifier parsed;
  const std::string type = mod.value("type", "add");
  if (type == "override") {
    parsed.type = ModifierType::Override;
    parsed.value = mod.value("value", 0);
  } else if (type == "multiply") {
    parsed.type = ModifierType::Multiply;
    parsed.factor = mod.value("factor", mod.value("value", 1.0));
  } else if (type == "clamp") {
    parsed.type = ModifierType::Clamp;
    parsed.clampMin = mod.value("clamp_min", mod.value("min", 0));
    parsed.clampMax = mod.value("clamp_max", mod.value("max", 0));
  } else { // "add"
    parsed.type = ModifierType::Add;
    parsed.value = mod.value("value", 0);
  }
  return parsed;
}

/// EvalContext for universal mode. Resolves:
///   - bare ids and `actor.X`  -> the actor's stats,
///   - `target.X`              -> the (optional) target's stats,
///   - `effective.X`           -> the target's *effective* stat (raw value plus
///                                equipped-gear and condition modifiers; the
///                                actor when there is no target),
///   - `env.X`                 -> values from an environment bag,
///   - `event.X` / `action.X`  -> values from a parameters bag.
///
/// @par Why one context for all four namespaces?
/// A single formula may legitimately mix scopes (a damage-reduction formula
/// reading both @c actor and @c event). One resolver that knows all four
/// namespaces lets the engine evaluate such formulas naturally, without
/// composing multiple contexts.
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

/**
 * A universal-mode entity: dynamic stats, resources, and conditions.
 *
 * @par Why "dynamic"?
 * Unlike the generated characters, this class can represent *any* ruleset's
 * entities at runtime. It is the workhorse of universal mode, and its
 * flexibility (no recompilation for a new game) is traded against the
 * compile-time guarantees of the specific mode.
 */
class DynamicEntity {
public:
  /// Creates an empty entity bound to `ruleset`. Entities start statless and
  /// are typically populated via @ref loadFromArchetype afterwards.
  DynamicEntity(const Ruleset &ruleset, std::string id)
      : m_ruleset(&ruleset), m_id(std::move(id)) {}

  /// The entity's identifier (e.g. the archetype id). Kept for diagnostics
  /// and for payloads that need to name the actor (e.g. @c attacker_id).
  [[nodiscard]] const std::string &id() const noexcept {
    return m_id;
  }

  /// StatProvider: returns an attribute, skill, or derived stat value.
  ///
  /// @par Why derive-on-demand instead of precomputing?
  /// Derived stats depend on the current base stats (a CON-draining effect
  /// changes the pool maximum). Computing on read keeps the answer always
  /// consistent with the latest mutations, at the cost of re-evaluating the
  /// formula — the intended trade-off for the dynamic mode.
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
  /// Unlike @ref getStat this never evaluates a derived formula — it reads
  /// only the stored base values, which is what bestiary promotion needs.
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
  /// amount actually applied (see @ref ResourcePool::modify). Missing pools
  /// are a no-op returning 0, so damage against an undeclared resource cannot
  /// crash a caller.
  [[nodiscard]] int32_t modifyResource(std::string_view resourceId, int32_t delta) {
    const auto it = m_resources.find(std::string(resourceId));
    return it == m_resources.end() ? 0 : it->second.modify(delta);
  }

  /// Adds `stacks` of a condition (stacks accumulate).
  ///
  /// @par Why stacks?
  /// Several rulesets model conditions that stack (multiple poison doses, or
  /// repeated applications). Counting stacks in the map keeps that information
  /// without a separate per-condition structure.
  void addCondition(std::string_view conditionId, int32_t stacks = 1) {
    m_conditions[std::string(conditionId)] += stacks;
  }

  /// Removes a condition entirely.
  void removeCondition(std::string_view conditionId) {
    m_conditions.erase(std::string(conditionId));
  }

  /// Whether the entity currently has the condition. A condition with stacks
  /// reduced to 0 (or removed) is treated as absent.
  [[nodiscard]] bool hasCondition(std::string_view conditionId) const {
    const auto it = m_conditions.find(std::string(conditionId));
    return it != m_conditions.end() && it->second > 0;
  }

  /// Stack count of a condition (0 when absent).
  [[nodiscard]] int32_t conditionStacks(std::string_view conditionId) const {
    const auto it = m_conditions.find(std::string(conditionId));
    return it == m_conditions.end() ? 0 : it->second;
  }

  /// The raw condition -> stack-count map (immutable view).
  [[nodiscard]] const std::unordered_map<std::string, int32_t> &conditions() const noexcept {
    return m_conditions;
  }

  /// The effective value of `statId`: the raw value plus every modifier the
  /// sheet's equipped gear and active conditions contribute (through the
  /// shared modifier pipeline in @c core/modifier.hpp). This is the value
  /// checks and combat should resolve against.
  ///
  /// @par Why not replace getStat with this?
  /// Derived-stat formulas evaluate other stats and must not re-apply gear
  /// bonuses (an equipped +1 STR must not inflate STR-derived stats *and*
  /// feed back). @ref getStat stays the raw, formula-safe value; this is the
  /// gameplay-facing number.
  [[nodiscard]] int32_t getEffectiveStat(std::string_view statId) const {
    return applyModifierPipeline(getStat(statId), modifiersFor(statId));
  }

  /// The sheet's owned items (flat inventory).
  [[nodiscard]] Inventory &inventory() noexcept {
    return m_inventory;
  }
  [[nodiscard]] const Inventory &inventory() const noexcept {
    return m_inventory;
  }

  /// The sheet's worn gear (slot -> item).
  [[nodiscard]] Equipment &equipment() noexcept {
    return m_equipment;
  }
  [[nodiscard]] const Equipment &equipment() const noexcept {
    return m_equipment;
  }

  /// The sheet's wealth.
  [[nodiscard]] Money &money() noexcept {
    return m_money;
  }
  [[nodiscard]] const Money &money() const noexcept {
    return m_money;
  }

  /// Current Temporary Hit Points (a damage buffer that is consumed before the
  /// real hit-point pool; see @ref RulesetEngine::applyDamage).
  [[nodiscard]] int32_t temporaryHitPoints() const noexcept {
    return m_tempHp;
  }
  /// Replaces the Temporary Hit Points with `value` (never below 0).
  void setTemporaryHitPoints(int32_t value) {
    m_tempHp = value > 0 ? value : 0;
  }
  /// Adds `delta` to Temporary Hit Points (clamped at 0; a positive delta
  /// keeps the higher of the current and new pool, matching "they don't
  /// stack, you keep the higher" D&D rule).
  void addTemporaryHitPoints(int32_t delta) {
    m_tempHp = std::max<int32_t>(0, m_tempHp + delta);
  }

  /// The sheet's known / prepared spells.
  [[nodiscard]] Spellbook &spellbook() noexcept {
    return m_spellbook;
  }
  [[nodiscard]] const Spellbook &spellbook() const noexcept {
    return m_spellbook;
  }

  /// The sheet's experience / advancement state.
  [[nodiscard]] Advancement &advancement() noexcept {
    return m_advancement;
  }
  [[nodiscard]] const Advancement &advancement() const noexcept {
    return m_advancement;
  }

  /// The sheet's active-effect timeline (conditions with durations).
  [[nodiscard]] EffectTimeline &effects() noexcept {
    return m_effects;
  }
  [[nodiscard]] const EffectTimeline &effects() const noexcept {
    return m_effects;
  }

  /// Active afflictions (poisons / diseases / curses) applied to the sheet.
  struct AppliedAffliction {
    std::string section;                 ///< "poisons" / "diseases" / "curses"
    std::string id;                      ///< affliction record id
    std::vector<std::string> conditions; ///< condition ids its effects applied
  };

  [[nodiscard]] std::vector<AppliedAffliction> &afflictions() noexcept {
    return m_afflictions;
  }
  [[nodiscard]] const std::vector<AppliedAffliction> &afflictions() const noexcept {
    return m_afflictions;
  }

  /// Records an applied affliction (called by the engine's applyAffliction).
  void addAffliction(std::string_view section, std::string_view id,
                     const std::vector<std::string> &conditions = {}) {
    m_afflictions.push_back(AppliedAffliction{std::string(section), std::string(id), conditions});
  }

  /// Whether the sheet is resistant (or immune) to a damage type (e.g.
  /// "Ranged" from a magic shield). Populated by structured effects.
  [[nodiscard]] bool hasResistance(std::string_view type) const {
    return m_resistances.find(std::string(type)) != m_resistances.end();
  }
  /// Marks the sheet as resistant to `type`.
  void addResistance(std::string_view type) {
    m_resistances.insert(std::string(type));
  }
  /// Removes the resistance to `type`.
  void removeResistance(std::string_view type) {
    m_resistances.erase(std::string(type));
  }
  /// All damage types the sheet is resistant to (immutable view).
  [[nodiscard]] const std::unordered_set<std::string> &resistances() const noexcept {
    return m_resistances;
  }

  /// Serializes the whole sheet (stats, resources, conditions, inventory,
  /// equipment, money, spellbook, advancement, effects) to a JSON object —
  /// the save-game form. @ref id is included but restored by the caller
  /// (it is fixed at construction).
  void toJson(Json &out) const {
    out = Json::object();
    out["id"] = m_id;
    out["stats"] = m_stats;
    Json resources = Json::object();
    for (const auto &[id, pool] : m_resources) {
      resources[id] = pool.current;
    }
    out["resources"] = resources;
    out["conditions"] = m_conditions;
    Json inventoryJson;
    m_inventory.toJson(inventoryJson);
    out["inventory"] = inventoryJson;
    Json equipmentJson;
    m_equipment.toJson(equipmentJson);
    out["equipment"] = equipmentJson;
    out["money"] = m_money.baseUnits();
    out["temp_hp"] = m_tempHp;
    Json spellbookJson;
    m_spellbook.toJson(spellbookJson);
    out["spellbook"] = spellbookJson;
    Json advancementJson;
    m_advancement.toJson(advancementJson);
    out["advancement"] = advancementJson;
    Json effectsJson;
    m_effects.toJson(effectsJson);
    out["effects"] = effectsJson;
    Json afflictions = Json::array();
    for (const AppliedAffliction &aff : m_afflictions) {
      afflictions.push_back(
          {{"section", aff.section}, {"id", aff.id}, {"conditions", aff.conditions}});
    }
    out["afflictions"] = afflictions;
    Json resistances = Json::array();
    for (const std::string &type : m_resistances) {
      resistances.push_back(type);
    }
    out["resistances"] = resistances;
  }

  /// Restores the sheet from the JSON form produced by @ref toJson (the id is
  /// kept from construction). State not present in `in` is left unchanged.
  void fromJson(const Json &in) {
    if (!in.is_object()) {
      return;
    }
    if (in.contains("stats") && in.at("stats").is_object()) {
      m_stats = in.at("stats").get<std::unordered_map<std::string, int32_t>>();
    }
    if (in.contains("conditions") && in.at("conditions").is_object()) {
      m_conditions = in.at("conditions").get<std::unordered_map<std::string, int32_t>>();
    }
    if (in.contains("resources") && in.at("resources").is_object()) {
      for (const auto &[id, value] : in.at("resources").items()) {
        m_resources[id].current = value.get<int32_t>();
      }
    }
    if (in.contains("inventory")) {
      m_inventory.fromJson(in.at("inventory"));
    }
    if (in.contains("equipment")) {
      m_equipment.fromJson(in.at("equipment"));
    }
    if (in.contains("money") && in.at("money").is_number_integer()) {
      m_money = Money{in.at("money").get<int64_t>()};
    }
    if (in.contains("temp_hp") && in.at("temp_hp").is_number_integer()) {
      m_tempHp = in.at("temp_hp").get<int32_t>();
    }
    if (in.contains("spellbook")) {
      m_spellbook.fromJson(in.at("spellbook"));
    }
    if (in.contains("advancement")) {
      m_advancement.fromJson(in.at("advancement"));
    }
    if (in.contains("effects")) {
      m_effects.fromJson(in.at("effects"));
    }
    if (in.contains("afflictions") && in.at("afflictions").is_array()) {
      m_afflictions.clear();
      for (const Json &entry : in.at("afflictions")) {
        AppliedAffliction aff;
        aff.section = entry.value("section", "");
        aff.id = entry.value("id", "");
        if (entry.contains("conditions") && entry.at("conditions").is_array()) {
          for (const Json &cond : entry.at("conditions")) {
            aff.conditions.push_back(cond.get<std::string>());
          }
        }
        m_afflictions.push_back(std::move(aff));
      }
    }
    if (in.contains("resistances") && in.at("resistances").is_array()) {
      m_resistances.clear();
      for (const Json &type : in.at("resistances")) {
        m_resistances.insert(type.get<std::string>());
      }
    }
  }

  /// Loads attributes, skills, resources, and conditions from an archetype
  /// JSON record (ranged values picked at random), then (re)initializes the
  /// resource pools. This overload uses a fresh entropy-seeded RNG.
  void loadFromArchetype(const Json &archetype) {
    DefaultRandom rng;
    loadFromArchetype(archetype, Variance::Random, rng);
  }

  /// As above, but picks ranged values according to `variance` (weakest, weak,
  /// average, strong, strongest, or random) with the caller-supplied RNG.
  /// The template overload exists so deterministic tests and Monte-Carlo runs
  /// can inject a scripted/seeded RNG.
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
    // Bookkeeping fields an archetype (or bestiary entry) may declare. Each is
    // optional: a ruleset without money / gear / spells simply omits them.
    if (archetype.contains("wealth")) {
      const Json &wealth = archetype.at("wealth");
      if (wealth.is_object() && !m_ruleset->currencySystem.id.empty()) {
        int64_t total = 0;
        for (const auto &[denom, qty] : wealth.items()) {
          total += m_ruleset->currencySystem.valueOf(denom, qty.get<int64_t>());
        }
        m_money = Money{total};
      } else if (wealth.is_number_integer()) {
        m_money = Money{wealth.get<int64_t>()};
      }
    }
    if (archetype.contains("equipment")) {
      const Json &equip = archetype.at("equipment");
      if (equip.is_object()) {
        for (const auto &[slot, item] : equip.items()) {
          (void)m_equipment.equip(slot, item.get<std::string>());
        }
      } else if (equip.is_array()) {
        for (const Json &entry : equip) {
          (void)m_equipment.equip(entry.value("slot", ""), entry.value("item", ""));
        }
      }
    }
    if (archetype.contains("spells_known")) {
      for (const Json &spellId : archetype.at("spells_known")) {
        m_spellbook.learn(spellId.get<std::string>());
      }
    }
    if (archetype.contains("xp")) {
      m_advancement.gainXp(archetype.at("xp").get<int64_t>());
    }
    if (archetype.contains("level")) {
      m_advancement.level = archetype.at("level").get<int32_t>();
    }
  }

private:
  /// Collects the modifier steps affecting `statId` from the sheet's equipped
  /// items and active conditions. Gear modifiers apply once per equipped item;
  /// a condition's per-stack modifiers scale additively with its stack count
  /// (Fear I-IV in The Dark Eye is "-1 per level on checks").
  [[nodiscard]] std::vector<Modifier> modifiersFor(std::string_view statId) const {
    std::vector<Modifier> result;
    for (const auto &[slot, itemId] : m_equipment.slots()) {
      const Json *item = findDataRecord("items", itemId);
      if (item == nullptr || !item->contains("modifiers") || !item->at("modifiers").is_array()) {
        continue;
      }
      for (const Json &mod : item->at("modifiers")) {
        if (mod.value("target_stat", "") == statId) {
          result.push_back(parseModifierJson(mod));
        }
      }
    }
    for (const auto &[conditionId, stacks] : m_conditions) {
      if (stacks <= 0) {
        continue;
      }
      const Json *cond = findDataRecord("conditions", conditionId);
      if (cond == nullptr || !cond->contains("stat_modifiers") ||
          !cond->at("stat_modifiers").is_array()) {
        continue;
      }
      for (const Json &mod : cond->at("stat_modifiers")) {
        if (mod.value("stat", "") == statId) {
          Modifier parsed = parseModifierJson(mod);
          if (parsed.type == ModifierType::Add) {
            parsed.value *= stacks;
          }
          result.push_back(parsed);
        }
      }
    }
    return result;
  }

  /// Looks up a raw data record by id in a `data` section (items, conditions,
  /// ...) — the sheet-side twin of the engine's record finders.
  [[nodiscard]] const Json *findDataRecord(std::string_view section, std::string_view id) const {
    if (!m_ruleset->data.is_object() || !m_ruleset->data.contains(section) ||
        !m_ruleset->data.at(section).is_array()) {
      return nullptr;
    }
    for (const Json &record : m_ruleset->data.at(section)) {
      if (record.value("id", "") == id) {
        return &record;
      }
    }
    return nullptr;
  }
  /// (Re)creates the resource pools from the ruleset definitions, computing
  /// each maximum from the current stats. Called after loading an archetype
  /// so a pool always starts at its full derived maximum — the default state
  /// a freshly spawned character is expected to be in.
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
  /// An empty environment/parameters bag keeps the evaluation total even
  /// though this path only ever resolves bare stat ids.
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
  Inventory m_inventory;
  Equipment m_equipment;
  Money m_money;
  int32_t m_tempHp{0};
  Spellbook m_spellbook;
  Advancement m_advancement;
  EffectTimeline m_effects;
  std::vector<AppliedAffliction> m_afflictions;
  std::unordered_set<std::string> m_resistances;
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
  if (prefix == "effective") {
    const DynamicEntity &subject = m_target != nullptr ? *m_target : m_actor;
    out = static_cast<double>(subject.getEffectiveStat(rest));
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

/// A StatProvider adapter that resolves stats through a sheet's effective
/// value (base + equipped-item modifiers + active-condition modifiers) instead
/// of the raw stored value.
///
/// @par Why a wrapper instead of changing getStat?
/// Derived-stat formulas must read raw values (an equipped +1 STR must not
/// feed back into STR itself through a formula). Keeping @ref DynamicEntity::
/// getStat raw and wrapping it for *resolution* lets checks and combat see the
/// gear-influenced number while formulas stay stable.
struct EffectiveStatProvider {
  const DynamicEntity &entity;
  [[nodiscard]] int32_t getStat(std::string_view statId) const {
    return entity.getEffectiveStat(statId);
  }
};

static_assert(StatProvider<DynamicEntity>);

} // namespace rpg_os
