// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_ruleset_loader.cpp
 * @brief Tests for the ruleset loader and model (@c rpg_os::RulesetLoader,
 * @c rpg_os::Ruleset).
 *
 * Verifies parsing of every schema section, the strict validation rules
 * (duplicate ids, unknown stat references, derived-stat cycles, missing
 * required fields such as @c licence), and the error reporting contract — a
 * malformed ruleset must fail load with a descriptive
 * @c std::invalid_argument rather than load a half-broken model.
 */
#include <doctest/doctest.h>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

using rpg_os::Ruleset;
using rpg_os::RulesetLoader;

constexpr std::string_view kValidRuleset = R"json(
{
  "schema_version": 1,
  "ruleset_id": "test_ruleset",
  "ruleset_name": "Test Ruleset",
  "source": "test",
  "licence": "CC-BY-4.0",
  "licence_source": "https://example.com/licence",
  "licence_notice": "Example notice text.",
  "attribution": "Example attribution.",
  "comment": "A minimal fixture for loader tests.",
  "namespace": "rpg_os::generated::test",
  "attributes": [
    { "id": "COU", "name": "Courage", "min": 1, "max": 21, "default": 10 },
    { "id": "AGI", "name": "Agility", "min": 1, "max": 21, "default": 10 },
    { "id": "STR", "name": "Strength", "min": 1, "max": 21, "default": 10 },
    { "id": "CN",  "name": "Constitution", "min": 1, "max": 21, "default": 10 }
  ],
  "derived_stats": [
    { "id": "Base_AT", "name": "Base Attack", "formula": "floor((COU + AGI + STR) / 5)" },
    { "id": "Vitality_Max", "name": "Max Vitality", "formula": "round((CN + CN + STR) / 2)" }
  ],
  "resource_pools": [
    { "id": "VP", "name": "Vitality Points", "max_stat": "Vitality_Max", "min": 0 }
  ],
  "skills": [
    { "id": "climbing", "name": "Climbing", "attributes": ["COU", "AGI", "STR"], "default": 0 }
  ],
  "check_types": {
    "dsa4_talent": {
      "resolution": "pool",
      "dice": "3d20",
      "pool_attributes": ["COU", "AGI", "STR"],
      "pool_stat": "climbing",
      "critical_style": "double",
      "fumble_style": "double",
      "grading": "pool_quality",
      "difficulty_mode": "to_stat"
    },
    "dsa4_attribute": {
      "resolution": "threshold",
      "dice": "1d20",
      "comparison": "le",
      "threshold_source": "actor_stat",
      "threshold_stat": "COU",
      "critical_style": "face",
      "critical_face": 1,
      "critical_confirm": true,
      "fumble_style": "face",
      "fumble_face": 20,
      "fumble_confirm": true,
      "grading": "none",
      "difficulty_mode": "to_stat"
    }
  },
  "cost_tables": {
    "DSA_Column_A": { "type": "multiplier", "base_factor": 1.0, "values": { "1": 1, "2": 2, "3": 4 } },
    "Levels": { "type": "threshold", "thresholds": [ { "key": 0, "value": 1 }, { "key": 300, "value": 2 } ] }
  },
  "equipment_slots": [
    { "id": "body_armor", "name": "Body Armor" }
  ],
  "event_triggers": [
    {
      "id": "wound_check",
      "trigger": "on_damage_taken",
      "condition": "event.damage > target.CN",
      "actions": [
        { "type": "apply_condition", "condition": "Wound", "stacks": "floor(event.damage / target.CN)" }
      ]
    }
  ],
  "data": {
    "archetypes": [
      { "id": "geron", "name": "Geron", "attributes": { "COU": 12, "AGI": 13, "STR": 11 },
        "skills": { "climbing": 7 } }
    ]
  }
}
)json";

Ruleset loadValid() {
  return RulesetLoader::loadFromString(kValidRuleset);
}

struct MapEvalContext : rpg_os::EvalContext {
  std::unordered_map<std::string, double> values;
  bool resolve(std::string_view path, double &out) const override {
    const auto it = values.find(std::string(path));
    if (it == values.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
};

} // namespace

TEST_CASE("RulesetLoader: loads a valid ruleset") {
  const Ruleset ruleset = loadValid();
  CHECK(ruleset.id == "test_ruleset");
  CHECK(ruleset.name == "Test Ruleset");
  CHECK(ruleset.schemaVersion == 1);
  CHECK(ruleset.cppNamespace == "rpg_os::generated::test");
  CHECK(ruleset.source == "test");
  CHECK(ruleset.licence == "CC-BY-4.0");
  CHECK(ruleset.licenceSource == "https://example.com/licence");
  CHECK(ruleset.licenceNotice == "Example notice text.");
  CHECK(ruleset.attribution == "Example attribution.");
  CHECK(ruleset.attributes.size() == 4);
  CHECK(ruleset.derivedStats.size() == 2);
  CHECK(ruleset.resourcePools.size() == 1);
  CHECK(ruleset.skills.size() == 1);
  CHECK(ruleset.checkTypes.size() == 2);
  CHECK(ruleset.costTables.size() == 2);
  CHECK(ruleset.equipmentSlots.size() == 1);
  CHECK(ruleset.eventTriggers.size() == 1);
}

TEST_CASE("RulesetLoader: attribute defaults and bounds") {
  const Ruleset ruleset = loadValid();
  const rpg_os::AttributeDef *cou = ruleset.findAttribute("COU");
  REQUIRE(cou != nullptr);
  CHECK(cou->name == "Courage");
  CHECK(cou->minValue == 1);
  CHECK(cou->maxValue == 21);
  CHECK(cou->defaultValue == 10);
  CHECK(ruleset.findAttribute("MISSING") == nullptr);
}

TEST_CASE("RulesetLoader: derived stat formula is parsed and evaluable") {
  const Ruleset ruleset = loadValid();
  const rpg_os::DerivedStatDef *baseAt = ruleset.findDerivedStat("Base_AT");
  REQUIRE(baseAt != nullptr);
  MapEvalContext context;
  context.values = {{"COU", 12.0}, {"AGI", 13.0}, {"STR", 11.0}};
  CHECK(baseAt->expression.evaluate(context) == doctest::Approx(7.0));
}

TEST_CASE("RulesetLoader: check type recipe is populated") {
  const Ruleset ruleset = loadValid();
  const rpg_os::CheckTypeDef *talent = ruleset.findCheckType("dsa4_talent");
  REQUIRE(talent != nullptr);
  CHECK(talent->recipe.resolution == rpg_os::Resolution::Pool);
  CHECK(talent->recipe.numPoolAttributes == 3);
  CHECK(talent->recipe.poolAttributes[0] == "COU");
  CHECK(talent->recipe.poolStat == "climbing");
  CHECK(talent->recipe.criticalStyle == rpg_os::CriticalStyle::DoubleRoll);
  CHECK(talent->recipe.grading == rpg_os::Grading::PoolQuality);
  const rpg_os::CheckTypeDef *attr = ruleset.findCheckType("dsa4_attribute");
  REQUIRE(attr != nullptr);
  CHECK(attr->recipe.criticalConfirm);
  CHECK(attr->recipe.fumbleConfirm);
  CHECK(ruleset.findCheckType("missing") == nullptr);
}

TEST_CASE("RulesetLoader: percentile d100 check kinds are parsed") {
  const Ruleset ruleset = RulesetLoader::loadFromString(R"json(
    {
      "schema_version": 1,
      "ruleset_id": "brp_demo",
      "licence": "demo",
      "attributes": [ { "id": "POW", "name": "Power" } ],
      "skills": [ { "id": "spot", "name": "Spot", "attributes": ["POW"], "default": 25 } ],
      "check_types": {
        "brp_check_pow": { "resolution": "threshold", "dice": "1d100", "comparison": "le", "threshold_source": "actor_stat", "threshold_stat": "POW", "critical_style": "percentile", "fumble_style": "percentile", "grading": "percentile", "difficulty_mode": "to_stat", "difficulty_multiplier": "double_halve" },
        "brp_combat": { "resolution": "opposed", "dice": "1d100", "attack_stat": "spot", "parry_stat": "POW", "compare_levels": true, "critical_style": "percentile", "fumble_style": "percentile", "grading": "percentile", "difficulty_mode": "to_stat" },
        "brp_resistance": { "resolution": "resistance", "dice": "1d100", "attack_stat": "POW", "parry_stat": "POW", "grading": "none", "difficulty_mode": "to_stat" }
      }
    }
  )json");
  const rpg_os::CheckTypeDef *pow = ruleset.findCheckType("brp_check_pow");
  REQUIRE(pow != nullptr);
  CHECK(pow->recipe.resolution == rpg_os::Resolution::Threshold);
  CHECK(pow->recipe.comparison == rpg_os::Comparison::LessEqual);
  CHECK(pow->recipe.thresholdStat == "POW");
  CHECK(pow->recipe.grading == rpg_os::Grading::Percentile);

  const rpg_os::CheckTypeDef *combat = ruleset.findCheckType("brp_combat");
  REQUIRE(combat != nullptr);
  CHECK(combat->recipe.resolution == rpg_os::Resolution::Opposed);
  CHECK(combat->recipe.compareLevels);
  CHECK(combat->recipe.attackStat == "spot");
  CHECK(combat->recipe.parryStat == "POW");

  const rpg_os::CheckTypeDef *resistance = ruleset.findCheckType("brp_resistance");
  REQUIRE(resistance != nullptr);
  CHECK(resistance->recipe.resolution == rpg_os::Resolution::Resistance);
  CHECK(resistance->recipe.attackStat == "POW");
  CHECK(resistance->recipe.parryStat == "POW");
}

TEST_CASE("RulesetLoader: cost tables are loaded") {
  const Ruleset ruleset = loadValid();
  const rpg_os::CostTableDef *columnA = ruleset.findCostTable("DSA_Column_A");
  REQUIRE(columnA != nullptr);
  CHECK(columnA->table.lookup(3) == 4);
  const rpg_os::CostTableDef *levels = ruleset.findCostTable("Levels");
  REQUIRE(levels != nullptr);
  CHECK(levels->table.lookup(300) == 2);
  CHECK(ruleset.findCostTable("missing") == nullptr);
}

TEST_CASE("RulesetLoader: hasStat covers attributes, derived stats, and skills") {
  const Ruleset ruleset = loadValid();
  CHECK(ruleset.hasStat("COU"));
  CHECK(ruleset.hasStat("Base_AT"));
  CHECK(ruleset.hasStat("climbing"));
  CHECK_FALSE(ruleset.hasStat("bogus"));
}

TEST_CASE("RulesetLoader: keeps the raw data section") {
  const Ruleset ruleset = loadValid();
  REQUIRE(ruleset.data.is_object());
  REQUIRE(ruleset.data.contains("archetypes"));
  REQUIRE(ruleset.data.at("archetypes").is_array());
  CHECK(ruleset.data.at("archetypes").size() == 1);
  CHECK(ruleset.data.at("archetypes")[0].at("id").get<std::string>() == "geron");
}

// ---------------------------------------------------------------------------
// Validation errors
// ---------------------------------------------------------------------------

TEST_CASE("RulesetLoader: missing required top-level fields throw") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"({ "ruleset_id": "x" })"), std::invalid_argument);
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"({ "schema_version": 1 })"),
                  std::invalid_argument);
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"(not json)"), std::exception);
}

TEST_CASE("RulesetLoader: licence is required, comment is optional") {
  // Everything except 'licence' -> invalid.
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x", "attributes": [] }
  )json"),
                  std::invalid_argument);
  // Empty licence -> invalid.
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x", "licence": "", "attributes": [] }
  )json"),
                  std::invalid_argument);

  // Licence present, no comment -> ok, comment empty.
  const rpg_os::Ruleset noComment = RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x", "licence": "MIT", "attributes": [] }
  )json");
  CHECK(noComment.licence == "MIT");
  CHECK(noComment.comment.empty());
  // licence_source is optional: absent -> empty.
  CHECK(noComment.licenceSource.empty());
  // licence_notice / attribution are optional too.
  CHECK(noComment.licenceNotice.empty());
  CHECK(noComment.attribution.empty());

  // Both present -> parsed.
  const rpg_os::Ruleset both = RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x", "licence": "MIT",
      "licence_source": "https://example.com/source",
      "licence_notice": "Notice text.", "attribution": "Credit text.",
      "comment": "hello", "attributes": [] }
  )json");
  CHECK(both.licence == "MIT");
  CHECK(both.comment == "hello");
  // licence_source is parsed when present.
  CHECK(both.licenceSource == "https://example.com/source");
  // licence_notice / attribution are parsed when present.
  CHECK(both.licenceNotice == "Notice text.");
  CHECK(both.attribution == "Credit text.");
}

TEST_CASE("RulesetLoader: duplicate ids throw") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "attributes": [
        { "id": "A", "name": "A" },
        { "id": "A", "name": "A2" }
      ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: derived stat referencing an unknown stat throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "attributes": [ { "id": "A", "name": "A" } ],
      "derived_stats": [ { "id": "B", "formula": "A + NOPE" } ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: malformed derived stat formula throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "attributes": [ { "id": "A", "name": "A" } ],
      "derived_stats": [ { "id": "B", "formula": "A +" } ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: derived stat cycle throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "attributes": [ { "id": "A", "name": "A" } ],
      "derived_stats": [
        { "id": "B", "formula": "C + 1" },
        { "id": "C", "formula": "B + 1" }
      ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: resource pool with unknown max_stat throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "resource_pools": [ { "id": "HP", "max_stat": "NOPE" } ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: skill with unknown attribute throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "attributes": [ { "id": "A", "name": "A" } ],
      "skills": [ { "id": "s", "attributes": ["A", "NOPE"] } ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: check type with unknown resolution throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "check_types": { "c": { "resolution": "mystery_roll" } } }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: check type referencing an unknown stat throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "check_types": { "c": { "resolution": "threshold", "threshold_source": "actor_stat", "threshold_stat": "AC" } } }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: unknown event trigger throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "event_triggers": [ { "id": "e", "trigger": "on_moon_phase" } ] }
  )json"),
                  std::invalid_argument);
}

TEST_CASE("RulesetLoader: event action with unknown type throws") {
  CHECK_THROWS_AS(RulesetLoader::loadFromString(R"json(
    { "schema_version": 1, "ruleset_id": "x",
      "event_triggers": [ { "id": "e", "trigger": "on_damage_taken",
                            "actions": [ { "type": "explode" } ] } ] }
  )json"),
                  std::invalid_argument);
}
