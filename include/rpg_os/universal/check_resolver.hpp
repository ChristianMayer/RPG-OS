// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Check resolver (universal mode).
//
// A thin runtime layer over the shared algorithms in core/checks.hpp: it looks
// up a named `check_types` config in the ruleset and dispatches through
// `resolveCheck`. Specific-mode generated code calls the algorithm templates
// directly instead, so no work is duplicated.
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
  template <StatProvider Target, RandomNumberGenerator Rng>
  static CheckResult resolve(const Ruleset &ruleset, const DynamicEntity &actor,
                             const Target &target, std::string_view checkTypeId,
                             const CheckParams &params, Rng &rng) {
    const CheckTypeDef *def = ruleset.findCheckType(checkTypeId);
    if (def == nullptr) {
      throw std::invalid_argument("unknown check type '" + std::string(checkTypeId) + "'");
    }
    return resolveCheck(actor, target, def->config, params, rng);
  }
};

} // namespace rpg_os
