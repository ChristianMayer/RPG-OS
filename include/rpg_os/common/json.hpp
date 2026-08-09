// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// JSON value type used throughout rpg_os.
//
// The engine is JSON-driven; all rulesets, event payloads, and data records
// are represented as this type. It is a thin alias over the vendored
// nlohmann/json single header so callers never include nlohmann directly.
#pragma once

#include <nlohmann/json.hpp>

namespace rpg_os {

/// JSON value type shared by the universal engine, event system, and
/// generated (specific-mode) data loaders.
using Json = nlohmann::json;

} // namespace rpg_os
