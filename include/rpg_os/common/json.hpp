// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file json.hpp
 * @ingroup rpg_os_core
 * @brief JSON value type used throughout rpg_os.
 *
 * The engine is JSON-driven end to end: rulesets, event payloads, environment
 * bags, and the entire data database (creatures, items, archetypes, spells)
 * are carried as JSON. Exposing one alias is deliberate:
 *   - it keeps the dependency surface minimal — callers include
 *     @c <rpg_os/common/json.hpp> instead of the vendored nlohmann header, so
 *     swapping the backing library later touches exactly one line here;
 *   - it gives the universal engine, the event system, and the generated
 *     (specific-mode) data loaders one identical value type to exchange, so
 *     no mode ever has to convert to a different JSON representation.
 *
 * The vendored @c nlohmann/json single header is never formatted or modified
 * (see @c include/rpg_os/third_party).
 */
#pragma once

#include <nlohmann/json.hpp>

namespace rpg_os {

/**
 * JSON value type shared by the universal engine, event system, and generated
 * (specific-mode) data loaders.
 *
 * @par Why alias instead of using nlohmann directly?
 * The ruleset loader, the AST evaluator context, the event payloads, and the
 * code-generated @c fromJson/@c fromArchetype loaders all exchange @c Json
 * values. A thin alias means the rest of the library can be written and read
 * without any mention of the concrete third-party type, and the Doxygen
 * documentation stays about the engine's own vocabulary rather than leaking
 * a dependency name into every signature.
 */
using Json = nlohmann::json;

} // namespace rpg_os
