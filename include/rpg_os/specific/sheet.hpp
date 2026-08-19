// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file sheet.hpp
 * @ingroup rpg_os_specific
 * @brief Shared CRTP base for generated character sheets.
 *
 * The code generator emits one strongly typed @c Character class per ruleset
 * (generated/&lt;ruleset&gt;_static.hpp). Every generated class shares a large
 * block of identical machinery — the static data-database loaders — which this
 * CRTP base implements once, parameterised over the concrete sheet type, so a
 * generated header only carries the parts that are genuinely ruleset-specific
 * (its named members, derived getters and check methods).
 */
#pragma once

#include <cstdint>
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <rpg_os/core/variance.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {
namespace specific {

/// CRTP base for generated character sheets: implements the static loaders
/// that every generated @c Character shares (loading archetypes, creatures and
/// free-form data sections from the ruleset JSON at runtime).
///
/// @tparam Derived the generated @c Character class. It must provide a default
/// constructor and a public `fromJson(const Json&, Variance, Rng&)` member.
///
/// @par Why CRTP?
/// The loaders return the concrete sheet type and build it through its own
/// `fromJson`, so they cannot live in a non-template base class. A CRTP base
/// gives every generated class the identical loaders without copying the code
/// into each generated header — the code generator emits only the
/// ruleset-specific parts.
template <typename Derived> class SheetBase {
public:
  /// Loads a single archetype by id; throws std::invalid_argument when missing.
  static Derived fromArchetype(const Json &rulesetJson, std::string_view id) {
    DefaultRandom rng;
    return fromArchetype(rulesetJson, id, Variance::Random, rng);
  }

  /// Loads a single archetype by id, picking ranged values per `variance`.
  template <RandomNumberGenerator Rng>
  static Derived fromArchetype(const Json &rulesetJson, std::string_view id, Variance variance,
                               Rng &rng) {
    for (const auto &record : rulesetJson.at("data").at("archetypes")) {
      if (record.value("id", "") == id) {
        Derived character;
        character.fromJson(record, variance, rng);
        return character;
      }
    }
    throw std::invalid_argument("unknown archetype '" + std::string(id) + "'");
  }

  /// Loads every archetype record from the ruleset JSON's data section.
  static std::vector<Derived> loadArchetypes(const Json &rulesetJson) {
    DefaultRandom rng;
    return loadArchetypes(rulesetJson, Variance::Random, rng);
  }

  /// Loads every archetype record, picking ranged values per `variance`.
  template <RandomNumberGenerator Rng>
  static std::vector<Derived> loadArchetypes(const Json &rulesetJson, Variance variance, Rng &rng) {
    std::vector<Derived> out;
    for (const auto &record : rulesetJson.at("data").at("archetypes")) {
      Derived character;
      character.fromJson(record, variance, rng);
      out.push_back(std::move(character));
    }
    return out;
  }

  /// Loads a single creature from data.creatures by id; throws
  /// std::invalid_argument when missing (ranged values at random).
  static Derived fromCreature(const Json &rulesetJson, std::string_view id) {
    DefaultRandom rng;
    return fromCreature(rulesetJson, id, Variance::Random, rng);
  }

  /// Loads a single creature by id, picking ranged values per `variance`.
  template <RandomNumberGenerator Rng>
  static Derived fromCreature(const Json &rulesetJson, std::string_view id, Variance variance,
                              Rng &rng) {
    for (const auto &record : rulesetJson.at("data").at("creatures")) {
      if (record.value("id", "") == id) {
        Derived character;
        character.fromJson(record, variance, rng);
        return character;
      }
    }
    throw std::invalid_argument("unknown creature '" + std::string(id) + "'");
  }

  /// Loads every creature record from the ruleset JSON's data section.
  static std::vector<Derived> loadCreatures(const Json &rulesetJson) {
    DefaultRandom rng;
    return loadCreatures(rulesetJson, Variance::Random, rng);
  }

  /// Loads every creature record, picking ranged values per `variance`.
  template <RandomNumberGenerator Rng>
  static std::vector<Derived> loadCreatures(const Json &rulesetJson, Variance variance, Rng &rng) {
    std::vector<Derived> out;
    for (const auto &record : rulesetJson.at("data").at("creatures")) {
      Derived character;
      character.fromJson(record, variance, rng);
      out.push_back(std::move(character));
    }
    return out;
  }

  /// Loads every record from a named `data` section as raw JSON.
  static std::vector<Json> loadSection(const Json &rulesetJson, std::string_view section) {
    std::vector<Json> out;
    const auto &data = rulesetJson.at("data");
    if (data.contains(section)) {
      for (const auto &record : data.at(section)) {
        out.push_back(record);
      }
    }
    return out;
  }

  /// Loads every spells record from the ruleset JSON.
  static std::vector<Json> loadSpells(const Json &rulesetJson) {
    return loadSection(rulesetJson, "spells");
  }

  /// Loads every conditions record from the ruleset JSON.
  static std::vector<Json> loadConditions(const Json &rulesetJson) {
    return loadSection(rulesetJson, "conditions");
  }

  /// Loads every poisons record from the ruleset JSON.
  static std::vector<Json> loadPoisons(const Json &rulesetJson) {
    return loadSection(rulesetJson, "poisons");
  }

  /// Loads every diseases record from the ruleset JSON.
  static std::vector<Json> loadDiseases(const Json &rulesetJson) {
    return loadSection(rulesetJson, "diseases");
  }

  /// Loads every items record from the ruleset JSON.
  static std::vector<Json> loadItems(const Json &rulesetJson) {
    return loadSection(rulesetJson, "items");
  }

  /// Loads every curses record from the ruleset JSON.
  static std::vector<Json> loadCurses(const Json &rulesetJson) {
    return loadSection(rulesetJson, "curses");
  }

protected:
  /// Sheets are only ever used through a concrete generated type.
  ~SheetBase() = default;
};

} // namespace specific
} // namespace rpg_os
