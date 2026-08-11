// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ruleset_loader.hpp
 * @brief Ruleset loading and validation (universal / dynamic mode).
 *
 * Parses a ruleset JSON document into a @c Ruleset model: the schema
 * (attributes, derived stats, resources, skills, check types, cost tables,
 * equipment slots, event triggers) plus the raw @c data section (archetypes,
 * items, creatures) which stays as JSON and is read at runtime.
 *
 * @par Why keep the data section as raw JSON?
 * The schema describes *how to compute*; the data section is a database that
 * the engine reads per-entity (a monster's hit-point dice, an item's price).
 * Keeping it as JSON means the engine never materialises the whole bestiary
 * as C++ objects at load time, and new data needs no code change — and the
 * generated specific-mode code can parse the same section through its own
 * strongly typed loaders, which is exactly what the cross-mode parity tests
 * rely on.
 *
 * @par Why validate so aggressively at load time?
 * A ruleset is a data contract: a typo in an attribute id, a formula that
 * references a missing stat, or a cyclic derived-stat chain would otherwise
 * surface mid-session as a confusing runtime error. Validating everything up
 * front (duplicate ids, parseable formulas, resolvable stat references,
 * acyclic derived-stat graphs) turns a bad ruleset into a clear, immediate
 * @c std::invalid_argument at load — and lets the code generator assume the
 * schema is sound.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <rpg_os/common/event_system.hpp>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/checks.hpp>
#include <rpg_os/core/cost_table.hpp>
#include <rpg_os/universal/expression.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rpg_os {

/**
 * A core attribute definition from a ruleset.
 *
 * @par Why keep min/max/default on the definition?
 * Attributes are the raw inputs to everything else, and their legal band is a
 * ruleset-level decision (The Dark Eye allows 1–30, D&D 5e scores vary).
 * Storing the band lets validation and any UI/clamp logic agree with the
 * source book without hard-coding a game system into the engine.
 */
struct AttributeDef {
  std::string id;
  std::string name;
  int32_t minValue{1};
  int32_t maxValue{30};
  int32_t defaultValue{10};
};

/// A derived stat: an attribute (or other stat) computed from a formula.
///
/// @par Why store both the raw text and the parsed form?
/// The text is kept for diagnostics and for the code generator (which compiles
/// the formula to C++); the parsed @c Expression is what the universal engine
/// evaluates at runtime. Keeping both avoids re-parsing on every evaluation.
struct DerivedStatDef {
  std::string id;
  std::string name;
  std::string formulaText;
  Expression expression; ///< parsed formula
};

/// A resource pool definition (e.g. Hit Points, Astral Energy).
///
/// @par Why is the maximum a *stat id* rather than a number?
/// In the source games a pool's size is itself derived (TDE: LP = 5 + 2*CON;
/// D&D: HP depends on level and CON). Referencing a stat id defers the actual
/// computation to the entity, so the pool maximum always reflects the current
/// stats — including after a CON-draining effect.
struct ResourcePoolDef {
  std::string id;
  std::string name;
  std::string maxStat; ///< stat id that provides the pool maximum
  int32_t minValue{0};
};

/// A skill / talent definition (e.g. a DSA talent with linked attributes).
///
/// @par Why do skills carry their own attribute list?
/// A DSA talent is checked against *its own* three attributes (Climbing rolls
/// COU/AGI/STR, Dancing DEX/AGI/COU). Keeping the list on the skill lets the
/// engine resolve a skill check generically from the definition — and lets
/// the code generator emit a named check method per skill with the right
/// attributes baked in.
struct SkillDef {
  std::string id;
  std::string name;
  std::vector<std::string> attributes;
  int32_t defaultValue{0};
};

/// A named check type: an id plus the configuration for the shared resolver.
///
/// @par Why name-check-types at all?
/// Rulesets reference checks by name (e.g. "dnd5e_attack_melee",
/// "tde_attack"). Mapping a name to a @ref CheckConfig lets the universal
/// engine resolve checks without hard-coding any game — and gives the code
/// generator the exact config it needs to emit named, compiled check methods.
struct CheckTypeDef {
  std::string id;
  CheckConfig config;
};

/// A named cost / progression table.
struct CostTableDef {
  std::string id;
  CostTable table;
};

/// One action inside an event trigger.
///
/// @par Why one struct with all fields instead of per-type structs?
/// The action types are few ("modify_event_damage", "consume_resource",
/// "apply_condition") and each uses only a couple of fields. One struct with
/// the union of fields keeps the loader, the engine's executor, and the code
/// generator reading from the same shape — at the cost of a few unused
/// fields per action, which is negligible for ruleset-sized data.
struct EventActionDef {
  std::string type;        ///< "modify_event_damage" | "consume_resource" | "apply_condition"
  std::string resource;    ///< consume_resource: which resource
  int32_t amount{0};       ///< consume_resource: constant amount
  std::string formulaText; ///< modify_event_damage: damage expression
  Expression formula;      ///< parsed damage expression
  std::string condition;   ///< apply_condition: condition id
  std::string stacksText;  ///< apply_condition: stack count expression
  Expression stacks;       ///< parsed stack count expression
};

/// A rules-level reactive effect, e.g. the DSA wound check.
///
/// @par Why are triggers data, not C++?
/// Reactive rules vary per system (a wound check is TDE; D&D has no such
/// thing). Describing them as JSON-driven triggers means adding a new system
/// with new reactions requires only JSON — the engine's executor stays
/// system-agnostic.
struct EventTriggerDef {
  std::string id;
  EventType trigger{EventType::OnDamageTaken};
  std::string conditionText; ///< empty = always fires
  Expression condition;
  std::vector<EventActionDef> actions;
};

/**
 * The fully loaded model of one ruleset.
 *
 * @par Why a plain data class instead of hidden state?
 * A @ref Ruleset is a pure value: everything the engine or the code generator
 * needs is publicly readable. Making it a plain aggregate avoids an accessor
 * layer that would add nothing, and lets the universal engine, the loader, and
 * the code generator share it without ceremony.
 *
 * @par Lookup methods return raw pointers instead of optional/iterators
 * The lookup set is small and linear scans are fine at load/validation time;
 * returning `nullptr` for "absent" keeps call sites terse and lets the engine
 * distinguish "known, value 0" from "unknown".
 */
class Ruleset {
public:
  std::string id;
  std::string name;
  std::string source;
  std::string licence; ///< licence governing the ruleset content (required)
  std::string comment; ///< optional free-form note for the ruleset author
  std::string ns;      ///< namespace used by the code generator
  int32_t schemaVersion{0};

  std::vector<AttributeDef> attributes;
  std::vector<DerivedStatDef> derivedStats;
  std::vector<ResourcePoolDef> resourcePools;
  std::vector<SkillDef> skills;
  std::vector<CheckTypeDef> checkTypes;
  std::vector<CostTableDef> costTables;
  std::vector<std::string> equipmentSlots;
  std::vector<EventTriggerDef> eventTriggers;

  /// The raw `data` section (archetypes, items, creatures) as JSON.
  Json data{};

  /// Finds an attribute by id; returns nullptr when absent.
  [[nodiscard]] const AttributeDef *findAttribute(std::string_view statId) const;

  /// Finds a derived stat by id; returns nullptr when absent.
  [[nodiscard]] const DerivedStatDef *findDerivedStat(std::string_view statId) const;

  /// Finds a check type by id; returns nullptr when absent.
  [[nodiscard]] const CheckTypeDef *findCheckType(std::string_view statId) const;

  /// Finds a skill by id; returns nullptr when absent.
  [[nodiscard]] const SkillDef *findSkill(std::string_view statId) const;

  /// Finds a cost table by id; returns nullptr when absent.
  [[nodiscard]] const CostTableDef *findCostTable(std::string_view statId) const;

  /// Whether `statId` names a known stat (attribute, derived stat, or skill).
  /// Skills are counted here because the pool of a talent check is a stat the
  /// resolver must be able to read.
  [[nodiscard]] bool hasStat(std::string_view statId) const;
};

inline const AttributeDef *Ruleset::findAttribute(std::string_view statId) const {
  for (const AttributeDef &def : attributes) {
    if (def.id == statId) {
      return &def;
    }
  }
  return nullptr;
}

inline const DerivedStatDef *Ruleset::findDerivedStat(std::string_view statId) const {
  for (const DerivedStatDef &def : derivedStats) {
    if (def.id == statId) {
      return &def;
    }
  }
  return nullptr;
}

inline const CheckTypeDef *Ruleset::findCheckType(std::string_view statId) const {
  for (const CheckTypeDef &def : checkTypes) {
    if (def.id == statId) {
      return &def;
    }
  }
  return nullptr;
}

inline const SkillDef *Ruleset::findSkill(std::string_view statId) const {
  for (const SkillDef &def : skills) {
    if (def.id == statId) {
      return &def;
    }
  }
  return nullptr;
}

inline const CostTableDef *Ruleset::findCostTable(std::string_view statId) const {
  for (const CostTableDef &def : costTables) {
    if (def.id == statId) {
      return &def;
    }
  }
  return nullptr;
}

inline bool Ruleset::hasStat(std::string_view statId) const {
  if (findAttribute(statId) != nullptr) {
    return true;
  }
  if (findDerivedStat(statId) != nullptr) {
    return true;
  }
  for (const SkillDef &def : skills) {
    if (def.id == statId) {
      return true;
    }
  }
  return false;
}

/**
 * Loads and validates a ruleset JSON document.
 *
 * @par Why a static-only class (namespace-like)?
 * Loading is a pure function of the JSON input: it has no state worth keeping
 * in an object. A static-only interface communicates that and keeps the API
 * trivially testable — every parse stage is independently checkable via the
 * validation errors it raises.
 */
class RulesetLoader {
public:
  /// Parses and validates `root`. Throws std::invalid_argument on any error.
  ///
  /// @par Why throw instead of returning a result?
  /// A ruleset that cannot be loaded is unrecoverable for the engine that
  /// hosts it; failing loudly with a descriptive message is clearer than
  /// threading an error code through every consumer.
  static Ruleset load(const Json &root);

  /// Parses and validates a JSON string (convenience wrapper over @ref load).
  static Ruleset loadFromString(std::string_view jsonContent);

  /// Re-validates a loaded ruleset; throws std::invalid_argument on error.
  ///
  /// @par Why is validation re-runnable?
  /// Loading already validates, but a ruleset may be *edited* after load (e.g.
  /// by tooling or a live-edit app). Re-running validation lets such a caller
  /// check the mutated model without re-parsing from JSON.
  static void validate(const Ruleset &ruleset);

private:
  // One parser per schema section keeps each function short and its failure
  // messages local to the section being read — a new section only adds a
  // parser, it never touches the existing ones.
  static void parseAttributes(const Json &obj, Ruleset &out);
  static void parseDerivedStats(const Json &obj, Ruleset &out);
  static void parseResources(const Json &obj, Ruleset &out);
  static void parseSkills(const Json &obj, Ruleset &out);
  static void parseCheckTypes(const Json &obj, Ruleset &out);
  static void parseCostTables(const Json &obj, Ruleset &out);
  static void parseEquipmentSlots(const Json &obj, Ruleset &out);
  static void parseEventTriggers(const Json &obj, Ruleset &out);
  static CheckKind parseKind(std::string_view kind);
  static EventType parseTrigger(std::string_view trigger);

  static std::string error(std::string_view message);
  static void require(bool condition, std::string_view message);
};

inline std::string RulesetLoader::error(std::string_view message) {
  return "ruleset validation failed: " + std::string(message);
}

inline void RulesetLoader::require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::invalid_argument(error(message));
  }
}

inline CheckKind RulesetLoader::parseKind(std::string_view kind) {
  if (kind == "additive_d20") {
    return CheckKind::AdditiveD20;
  }
  if (kind == "roll_under_d20") {
    return CheckKind::RollUnderD20;
  }
  if (kind == "triple_roll_under_pool") {
    return CheckKind::TripleRollUnderPool;
  }
  if (kind == "attack_vs_defense") {
    return CheckKind::AttackVsDefense;
  }
  if (kind == "roll_under_d100") {
    return CheckKind::RollUnderD100;
  }
  if (kind == "opposed_roll_under_d100") {
    return CheckKind::OpposedRollUnderD100;
  }
  if (kind == "resistance_roll") {
    return CheckKind::ResistanceRoll;
  }
  throw std::invalid_argument(error("unknown check kind '" + std::string(kind) + "'"));
}

inline EventType RulesetLoader::parseTrigger(std::string_view trigger) {
  if (trigger == "on_before_check_roll") {
    return EventType::OnBeforeCheckRoll;
  }
  if (trigger == "on_after_check_roll") {
    return EventType::OnAfterCheckRoll;
  }
  if (trigger == "on_damage_calculated") {
    return EventType::OnDamageCalculated;
  }
  if (trigger == "on_damage_taken") {
    return EventType::OnDamageTaken;
  }
  if (trigger == "on_turn_start") {
    return EventType::OnTurnStart;
  }
  if (trigger == "on_turn_end") {
    return EventType::OnTurnEnd;
  }
  throw std::invalid_argument(error("unknown event trigger '" + std::string(trigger) + "'"));
}

inline void RulesetLoader::parseAttributes(const Json &obj, Ruleset &out) {
  if (!obj.contains("attributes")) {
    return;
  }
  for (const Json &item : obj.at("attributes")) {
    require(item.contains("id"), "attribute entry missing 'id'");
    require(item.contains("name"),
            "attribute '" + item.at("id").get<std::string>() + "' missing 'name'");
    AttributeDef def;
    def.id = item.at("id").get<std::string>();
    def.name = item.at("name").get<std::string>();
    def.minValue = item.value("min", 1);
    def.maxValue = item.value("max", 30);
    def.defaultValue = item.value("default", 10);
    out.attributes.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseDerivedStats(const Json &obj, Ruleset &out) {
  if (!obj.contains("derived_stats")) {
    return;
  }
  for (const Json &item : obj.at("derived_stats")) {
    require(item.contains("id"), "derived_stat entry missing 'id'");
    require(item.contains("formula"),
            "derived_stat '" + item.at("id").get<std::string>() + "' missing 'formula'");
    DerivedStatDef def;
    def.id = item.at("id").get<std::string>();
    def.name = item.value("name", def.id);
    def.formulaText = item.at("formula").get<std::string>();
    def.expression = Expression(def.formulaText);
    out.derivedStats.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseResources(const Json &obj, Ruleset &out) {
  if (!obj.contains("resource_pools")) {
    return;
  }
  for (const Json &item : obj.at("resource_pools")) {
    require(item.contains("id"), "resource_pool entry missing 'id'");
    require(item.contains("max_stat"),
            "resource_pool '" + item.at("id").get<std::string>() + "' missing 'max_stat'");
    ResourcePoolDef def;
    def.id = item.at("id").get<std::string>();
    def.name = item.value("name", def.id);
    def.maxStat = item.at("max_stat").get<std::string>();
    def.minValue = item.value("min", 0);
    out.resourcePools.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseSkills(const Json &obj, Ruleset &out) {
  if (!obj.contains("skills")) {
    return;
  }
  for (const Json &item : obj.at("skills")) {
    require(item.contains("id"), "skill entry missing 'id'");
    require(item.contains("attributes"),
            "skill '" + item.at("id").get<std::string>() + "' missing 'attributes'");
    SkillDef def;
    def.id = item.at("id").get<std::string>();
    def.name = item.value("name", def.id);
    def.defaultValue = item.value("default", 0);
    for (const Json &attr : item.at("attributes")) {
      def.attributes.push_back(attr.get<std::string>());
    }
    out.skills.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseCheckTypes(const Json &obj, Ruleset &out) {
  if (!obj.contains("check_types")) {
    return;
  }
  for (const auto &[key, item] : obj.at("check_types").items()) {
    require(item.contains("kind"), "check_type '" + key + "' missing 'kind'");
    CheckTypeDef def;
    def.id = key;
    def.config.kind = parseKind(item.at("kind").get<std::string>());
    def.config.diceExpression = item.value("dice_expression", "1d20");
    def.config.useConfirmationRoll = item.value("confirmation_roll", false);
    def.config.targetStat = item.value("target_stat", "");
    def.config.poolStat = item.value("pool_stat", "");
    def.config.attackStat = item.value("attack_stat", "");
    def.config.parryStat = item.value("parry_stat", "");
    if (item.contains("bonus_stats")) {
      for (const Json &stat : item.at("bonus_stats")) {
        def.config.bonusStats.push_back(stat.get<std::string>());
      }
    }
    if (item.contains("attributes")) {
      def.config.numAttributes = 0;
      for (const Json &attr : item.at("attributes")) {
        require(def.config.numAttributes < 3,
                "check_type '" + key + "' has more than 3 attributes");
        def.config.attributes[def.config.numAttributes++] = attr.get<std::string>();
      }
    }
    out.checkTypes.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseCostTables(const Json &obj, Ruleset &out) {
  if (!obj.contains("cost_tables")) {
    return;
  }
  for (const auto &[key, item] : obj.at("cost_tables").items()) {
    require(item.contains("type"), "cost_table '" + key + "' missing 'type'");
    const std::string type = item.at("type").get<std::string>();
    require(type == "threshold" || type == "multiplier",
            "cost_table '" + key + "' has unknown type '" + type + "'");
    CostTableDef def;
    def.id = key;
    const CostTable::Kind kind =
        type == "threshold" ? CostTable::Kind::Threshold : CostTable::Kind::Multiplier;
    const double baseFactor = item.value("base_factor", 1.0);
    std::vector<CostTable::Entry> entries;
    if (item.contains("thresholds")) {
      for (const Json &entry : item.at("thresholds")) {
        require(entry.contains("key") && entry.contains("value"),
                "cost_table '" + key + "' entry missing 'key' or 'value'");
        entries.push_back(
            CostTable::Entry{entry.at("key").get<int32_t>(), entry.at("value").get<int32_t>()});
      }
    } else if (item.contains("values")) {
      for (const auto &[idx, value] : item.at("values").items()) {
        const int32_t keyInt = static_cast<int32_t>(std::stoi(idx));
        entries.push_back(CostTable::Entry{keyInt, value.get<int32_t>()});
      }
    }
    def.table = CostTable::fromEntries(kind, baseFactor, std::move(entries));
    out.costTables.push_back(std::move(def));
  }
}

inline void RulesetLoader::parseEquipmentSlots(const Json &obj, Ruleset &out) {
  if (!obj.contains("equipment_slots")) {
    return;
  }
  for (const Json &slot : obj.at("equipment_slots")) {
    require(slot.contains("id"), "equipment_slot entry missing 'id'");
    out.equipmentSlots.push_back(slot.at("id").get<std::string>());
  }
}

inline void RulesetLoader::parseEventTriggers(const Json &obj, Ruleset &out) {
  if (!obj.contains("event_triggers")) {
    return;
  }
  for (const Json &item : obj.at("event_triggers")) {
    require(item.contains("id"), "event_trigger entry missing 'id'");
    require(item.contains("trigger"),
            "event_trigger '" + item.at("id").get<std::string>() + "' missing 'trigger'");
    EventTriggerDef def;
    def.id = item.at("id").get<std::string>();
    def.trigger = parseTrigger(item.at("trigger").get<std::string>());
    def.conditionText = item.value("condition", "");
    if (!def.conditionText.empty()) {
      def.condition = Expression(def.conditionText);
    }
    for (const Json &action : item.value("actions", Json::array())) {
      require(action.contains("type"), "event action missing 'type'");
      EventActionDef act;
      act.type = action.at("type").get<std::string>();
      act.resource = action.value("resource", "");
      act.amount = action.value("amount", 0);
      act.condition = action.value("condition", "");
      act.formulaText = action.value("formula", "");
      if (!act.formulaText.empty()) {
        act.formula = Expression(act.formulaText);
      }
      act.stacksText = action.value("stacks", "");
      if (!act.stacksText.empty()) {
        act.stacks = Expression(act.stacksText);
      }
      def.actions.push_back(std::move(act));
    }
    out.eventTriggers.push_back(std::move(def));
  }
}

inline Ruleset RulesetLoader::load(const Json &root) {
  require(root.is_object(), "ruleset root must be a JSON object");
  require(root.contains("schema_version"), "ruleset missing 'schema_version'");
  require(root.contains("ruleset_id"), "ruleset missing 'ruleset_id'");
  require(root.contains("licence"), "ruleset missing required 'licence'");
  require(root.at("licence").is_string() && !root.at("licence").get<std::string>().empty(),
          "ruleset 'licence' must be a non-empty string");

  Ruleset out;
  out.schemaVersion = root.at("schema_version").get<int32_t>();
  out.id = root.at("ruleset_id").get<std::string>();
  out.name = root.value("ruleset_name", out.id);
  out.source = root.value("source", "");
  out.licence = root.value("licence", "");
  out.comment = root.value("comment", "");
  out.ns = root.value("namespace", "rpg_os::generated::" + out.id);

  parseAttributes(root, out);
  parseDerivedStats(root, out);
  parseResources(root, out);
  parseSkills(root, out);
  parseCheckTypes(root, out);
  parseCostTables(root, out);
  parseEquipmentSlots(root, out);
  parseEventTriggers(root, out);
  if (root.contains("data")) {
    out.data = root.at("data");
  }

  validate(out);
  return out;
}

inline Ruleset RulesetLoader::loadFromString(std::string_view jsonContent) {
  Json root = Json::parse(jsonContent);
  return load(root);
}

inline void RulesetLoader::validate(const Ruleset &ruleset) {
  std::unordered_set<std::string> seen;
  // Attribute ids must be unique.
  for (const AttributeDef &def : ruleset.attributes) {
    require(seen.insert(def.id).second, "duplicate attribute id '" + def.id + "'");
  }
  for (const DerivedStatDef &def : ruleset.derivedStats) {
    require(seen.insert(def.id).second, "duplicate derived stat id '" + def.id + "'");
  }
  for (const SkillDef &def : ruleset.skills) {
    require(seen.insert(def.id).second, "duplicate skill id '" + def.id + "'");
  }
  std::unordered_set<std::string> checkIds;
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    require(checkIds.insert(def.id).second, "duplicate check type id '" + def.id + "'");
  }
  std::unordered_set<std::string> tableIds;
  for (const CostTableDef &def : ruleset.costTables) {
    require(tableIds.insert(def.id).second, "duplicate cost table id '" + def.id + "'");
  }

  // Derived-stat formulas: bare identifiers must be known stats.
  for (const DerivedStatDef &def : ruleset.derivedStats) {
    for (const std::string &ident : def.expression.identifiers()) {
      if (ident.find('.') != std::string::npos) {
        continue; // dotted paths (actor.*, target.*, env.*, ...) are allowed
      }
      require(ruleset.hasStat(ident),
              "derived stat '" + def.id + "' references unknown stat '" + ident + "'");
    }
  }

  // Derived-stat graph must be acyclic.
  {
    std::unordered_map<std::string, std::vector<std::string>> graph;
    for (const DerivedStatDef &def : ruleset.derivedStats) {
      for (const std::string &ident : def.expression.identifiers()) {
        if (ident.find('.') == std::string::npos && ruleset.findDerivedStat(ident) != nullptr) {
          graph[def.id].push_back(ident);
        }
      }
    }
    enum class State : uint8_t { Unvisited, Visiting, Done };
    std::unordered_map<std::string, State> state;
    std::function<void(const std::string &)> visit = [&](const std::string &nodeId) {
      state[nodeId] = State::Visiting;
      for (const std::string &dep : graph[nodeId]) {
        if (state[dep] == State::Visiting) {
          throw std::invalid_argument(error("derived stat cycle involving '" + nodeId + "'"));
        }
        if (state[dep] == State::Unvisited) {
          visit(dep);
        }
      }
      state[nodeId] = State::Done;
    };
    for (const DerivedStatDef &def : ruleset.derivedStats) {
      if (state[def.id] == State::Unvisited) {
        visit(def.id);
      }
    }
  }

  // Resource max stats must exist.
  for (const ResourcePoolDef &def : ruleset.resourcePools) {
    require(ruleset.hasStat(def.maxStat),
            "resource pool '" + def.id + "' references unknown max_stat '" + def.maxStat + "'");
  }

  // Skill attribute references must exist.
  for (const SkillDef &def : ruleset.skills) {
    for (const std::string &attr : def.attributes) {
      require(ruleset.findAttribute(attr) != nullptr,
              "skill '" + def.id + "' references unknown attribute '" + attr + "'");
    }
  }

  // Check type references must exist.
  for (const CheckTypeDef &def : ruleset.checkTypes) {
    const CheckConfig &cfg = def.config;
    for (const std::string &stat : cfg.bonusStats) {
      require(ruleset.hasStat(stat),
              "check type '" + def.id + "' references unknown bonus stat '" + stat + "'");
    }
    if (!cfg.targetStat.empty()) {
      require(ruleset.hasStat(cfg.targetStat), "check type '" + def.id +
                                                   "' references unknown target stat '" +
                                                   cfg.targetStat + "'");
    }
    if (!cfg.poolStat.empty()) {
      require(ruleset.hasStat(cfg.poolStat),
              "check type '" + def.id + "' references unknown pool stat '" + cfg.poolStat + "'");
    }
    if (!cfg.attackStat.empty()) {
      require(ruleset.hasStat(cfg.attackStat), "check type '" + def.id +
                                                   "' references unknown attack stat '" +
                                                   cfg.attackStat + "'");
    }
    if (!cfg.parryStat.empty()) {
      require(ruleset.hasStat(cfg.parryStat),
              "check type '" + def.id + "' references unknown parry stat '" + cfg.parryStat + "'");
    }
    for (std::size_t i = 0; i < cfg.numAttributes; ++i) {
      require(ruleset.hasStat(cfg.attributes[i]), "check type '" + def.id +
                                                      "' references unknown attribute '" +
                                                      cfg.attributes[i] + "'");
    }
  }

  // Event triggers: action types must be known (conditions and formulas were
  // already parsed by Expression at load time).
  for (const EventTriggerDef &def : ruleset.eventTriggers) {
    for (const EventActionDef &action : def.actions) {
      require(action.type == "modify_event_damage" || action.type == "consume_resource" ||
                  action.type == "apply_condition",
              "event action in '" + def.id + "' has unknown type '" + action.type + "'");
    }
  }
}

} // namespace rpg_os
