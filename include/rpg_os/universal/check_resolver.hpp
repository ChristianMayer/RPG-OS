// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file check_resolver.hpp
 * @brief Check resolver (universal mode).
 *
 * A thin runtime layer over the generic resolver in @c core/checks.hpp: it
 * looks up a named `check_types` recipe in the ruleset and dispatches through
 * @c resolveCheck. Specific-mode generated code calls @c resolveCheck with a
 * `constexpr` recipe directly, so no work is duplicated.
 *
 * @par Why a separate, near-empty class?
 * Keeping the name -> @c CheckRecipe lookup separate from the engine facade
 * gives the universal mode a tiny, independently testable seam: it answers
 * exactly "given a ruleset and a check name, resolve this check" and nothing
 * more. The engine composes it with entity creation, damage, and events.
 */
#pragma once

#include <rpg_os/core/checks.hpp>
#include <rpg_os/core/concepts.hpp>
#include <rpg_os/universal/dynamic_entity.hpp>
#include <rpg_os/universal/ruleset_loader.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rpg_os {

/// Resolves checks by ruleset-declared name.
class CheckResolver {
public:
  /// Resolves `checkTypeId` from `ruleset` for `actor` (and optional `target`).
  /// Throws std::invalid_argument for an unknown check type.
  ///
  /// @par Why throw on an unknown name?
  /// An unknown check type is a ruleset/authoring bug (a typo in a ruleset or
  /// a caller asking for a check the loaded system does not define). Failing
  /// fast with the offending name beats silently returning a "default" check
  /// that would resolve against the wrong mechanism.
  template <StatProvider Target, RandomNumberGenerator Rng>
  static CheckResult resolve(const Ruleset &ruleset, const DynamicEntity &actor,
                             const Target &target, std::string_view checkTypeId,
                             const CheckParams &params, Rng &rng) {
    const CheckTypeDef *def = ruleset.findCheckType(checkTypeId);
    if (def == nullptr) {
      throw std::invalid_argument("unknown check type '" + std::string(checkTypeId) + "'");
    }
    return resolveCheck(actor, target, def->recipe, params, rng);
  }
};

} // namespace rpg_os
