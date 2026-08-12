# AGENTS.md

Instructions for LLM coding agents (and human contributors) working in this
repository. **Read and follow these rules for every change you make.**

## Project

RPG-OS — a **C++23, header-only library**: a universal rule engine and
"operating system" for tabletop role-playing games. All rules live in
ruleset-specific JSON files; the engine resolves them in two modes:

- **Universal mode (dynamic):** `rpg_os::RulesetEngine` loads *any* ruleset
  JSON at runtime and resolves rules generically (AST formula evaluator,
  generic check resolver, modifier pipeline, cost tables, event system).
  Adding a new game system requires only a new JSON file — no recompilation.
- **Specific mode (codegen):** `codegen/rpg_os_codegen.py` compiles a ruleset's
  *schema* into a strongly typed header (`generated/<ruleset>.hpp`) with named
  members and methods (`courage()`, `baseAttack()`, `climbCheck()`, ...).
  Formulas are compiled to C++ and checks instantiate the shared templates
  with `constexpr` configs (zero dynamic allocation in the hot path). The
  generated code **still loads the JSON at runtime** for the *data* database
  (creatures, items, archetypes).

Both modes share one header-only, template-based core
(`include/rpg_os/core/`, `include/rpg_os/common/`).

## Ground rules

1. **Test-driven development (TDD) is mandatory.** Write a failing test first
   (red), implement the minimal code to pass (green), then refactor. Never add
   functionality without a test that exercises it. Keep the test suite green
   before finishing a change.
2. **Style is enforced — do not deviate.** Run `clang-format -i <file>` on
   every file you touch (config: `.clang-format`).
   - Indentation (shift width): **2 spaces**
   - Tab width: **8 columns**; use **spaces only**, never tab characters
   - Column limit: **100**
3. **Keep clang-tidy clean.** `.clang-tidy` enables `modernize-*`,
   `cppcoreguidelines-*`, `performance-*`, etc. Do not introduce new warnings;
   fix them before finishing. Never run the formatter/linter on vendored code
   under `include/rpg_os/third_party/`.
4. **Modern C++23 only.** Prefer:
   - `const` / `constexpr` where possible, `noexcept` where appropriate
   - value semantics and smart pointers — no manual `new`/`delete`
   - structured bindings, `std::ranges`, concepts where they improve clarity
   - `std::array`/`std::vector` over C arrays, `static_cast`/`reinterpret_cast`
     over C-style casts
   - `std::string_view`/`std::span` for non-owning parameters
   - templates / template metaprogramming to share algorithms between the
     universal and specific modes (see "Shared template core")
5. **Header-only first.** The library is header-only; keep it that way. Prefer
   `inline`/header-local state over out-of-line `.cpp` files. Reserve `src/`
   for a single documented exception if one ever becomes unavoidable.
6. **Don't break the build.** Configure, build, and test before finishing
   (commands below).

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:
- `-DRPG_OS_WARNINGS_AS_ERRORS=ON` — treat warnings as errors (enforced in CI)
- `-DRPG_OS_ENABLE_SANITIZERS=ON` — ASan + UBSan
- `-DRPG_OS_BUILD_TESTS=OFF` — skip the test suite
- `-DRPG_OS_RUN_CODEGEN=ON` — regenerate the codegen outputs during the build

Codegen (manual):

```sh
python3 codegen/rpg_os_codegen.py --ruleset rulesets/dnd5e_srd.json --out generated/
python3 codegen/rpg_os_codegen.py --ruleset rulesets/tde5e_core.json --out generated/
```

Generated headers are committed; keep them in sync with the rulesets. A CI
step regenerates them into a temp dir and checks `git diff --exit-code`.

## Project layout

| Path | Purpose |
| --- | --- |
| `include/rpg_os/common/` | Shared value types, JSON alias, event system |
| `include/rpg_os/core/` | Shared header-only template core (concepts, dice, math, modifier, cost tables, checks, entity) |
| `include/rpg_os/universal/` | Universal (dynamic) mode: ruleset loader, AST evaluator, dynamic entity, check resolver, engine facade |
| `include/rpg_os/specific/` | Runtime support used by generated code (`data_loader.hpp`) |
| `include/rpg_os/third_party/` | Vendored headers (`nlohmann/json.hpp`, `doctest/`) — never edit, never format |
| `src/` | Reserved for a future out-of-line TU; the library is header-only today |
| `codegen/` | `rpg_os_codegen.py` + `validate_ruleset.py` — Python 3 (stdlib only) code generator and schema validator |
| `rulesets/` | Ruleset JSON files (`dnd5e_srd.json`, `tde5e_core.json`) and `ruleset.schema.json` |
| `generated/` | Committed codegen outputs (`dnd5e_srd_static.hpp`, `tde5e_core_static.hpp`) |
| `tests/` | doctest suite, registered with CTest via `tests/CMakeLists.txt` |
| `examples/` | Small self-contained demo programs |
| `cmake/` | CMake helper modules (warnings, sanitizers) |

The `rpg_os` CMake target is an **INTERFACE (header-only)** target; do not
convert it to `STATIC`. Only tests and examples compile executables.

## Shared template core

The engine is one header-only template layer instantiated differently by each
mode:

- `core/concepts.hpp` defines `template<typename T> concept StatProvider`
  requiring `t.getStat(std::string_view) -> std::integral` — any integral
  storage width. Universal entities use a hash map of `int32_t`; generated
  characters store attributes/skills in the narrowest type that fits the
  ruleset bounds (`uint8_t` for the shipped rulesets) and satisfy the concept
  via a string→member switch (no allocations). Named getters are convenience
  API on top.
- `core/checks.hpp` is **ruleset-agnostic by design**: a check is a generic,
  data-driven `CheckRecipe` (resolution threshold/pool/opposed/resistance,
  dice, comparison, reference source, critical style, grading, difficulty
  handling) resolved by the single `resolveCheck` template. There is no
  `AdditiveD20`/`RollUnderD20`-style named mechanism anywhere in the core — a
  new ruleset, including one with a new combination of dice/comparison/
  grading, is expressed purely as recipe JSON. Universal mode interprets a
  runtime recipe; generated code builds a recipe from literals and calls the
  same `resolveCheck`. The three percentile variants are all configurations of
  the threshold/opposed/resistance resolutions (graded
  Critical/Special/Success/Fumble via `CheckResult::successLevel`, opposed
  level matrix, Resistance Table).
- Shared helpers: `core/dice_engine.hpp` (injectable RNG), `core/math.hpp`
  (floor/ceil/round/clamp used by the AST evaluator *and* generated code),
  `core/modifier.hpp` (base→override→add→multiply→clamp pipeline),
  `core/cost_table.hpp` (threshold + multiplier), `core/variance.hpp`
  (range selection: weakest / weak / average / strong / strongest / random
  via `rpg_os::readVariantValue`).
- A **parity test** asserts universal and specific modes produce identical
  results for the same scenario.

## Ruleset JSON conventions

- One file per system: `rulesets/<ruleset_id>.json`, containing the *schema*
  (rules) and a `data` section (creatures, items, archetypes, spells,
  conditions, poisons, diseases).
- Every ruleset is validated against `rulesets/ruleset.schema.json` (JSON
  Schema draft-07). `licence` is **required** (non-empty); `comment` is
  optional. Run `python3 codegen/validate_ruleset.py rulesets/*.json` after
  editing any ruleset; CI enforces it.
- Top level: `schema_version`, `ruleset_id`, `ruleset_name`, `source`,
  `licence` (required), `comment` (optional), `namespace` (used by codegen),
  `spell_resource` (optional: the resource pool spell casting draws its cost
  from; enables `RulesetEngine::castSpell`), then `attributes`,
  `derived_stats`, `resource_pools`, `skills`, `check_types`, `cost_tables`,
  `modifier_pipeline`, `equipment_slots`, `event_triggers`, `data`.
- Attribute ids are short uppercase codes (`COU`, `STR`); `name` is the human
  name used to derive C++ identifiers (`"Courage"` → `courage`).
- Derived-stat `formula` strings use the restricted grammar: arithmetic
  (`+ - * / % ^`), comparisons, `&& || !`, functions `min max floor ceil
  round clamp`, and identifiers (attribute ids, `actor.*`, `target.*`,
  `env.*`, parameters). The grammar must be translatable to C++ by codegen.
- `check_types` entries are named, data-driven `CheckRecipe`s (e.g.
  `dnd5e_attack_melee`, `tde_attack`) described entirely by generic fields:
  `resolution` (`threshold`/`pool`/`opposed`/`resistance`), `dice`,
  `comparison` (`ge`/`le`), `threshold_source`, `bonus_stats`,
  `pool_attributes`/`pool_stat`, `attack_stat`/`parry_stat`/`compare_levels`,
  `critical_style`/`critical_face`/`critical_confirm` and `fumble_*`,
  `grading`, `difficulty_mode`, `difficulty_multiplier`. The universal
  resolver interprets them; codegen emits named methods that build a
  `static const rpg_os::CheckRecipe` and call `rpg_os::resolveCheck`.
- **Ranges:** a numeric data value may be a plain integer, a dice expression
  string (`"2d6+4"`, kept verbatim from the source), or a range object
  `{"min": …, "max": …}`. Never collapse a source range into a single number.
  Range/stat values are resolved per `rpg_os::Variance` (weakest / weak /
  average / strong / strongest / random) via `rpg_os::readVariantValue`
  (`include/rpg_os/core/variance.hpp`).
- Adding a system = adding a JSON file; do not change engine code for it.

## Code conventions

- **Licence header:** every code file (headers, sources, Python scripts,
  CMake files) starts with
  `// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.`
  plus `// SPDX-License-Identifier: Apache-2.0` (use `#` for Python/CMake).
  Generated headers emit it automatically via the codegen banner.
- **Naming:** use `rpg_os` (never `rpgos`) wherever the project's name cannot
  be written as "RPG OS" — the C++ namespace is `rpg_os`, include paths are
  `<rpg_os/...>`, and CMake identifiers/options are `RPG_OS_*`.
- Header guards: `#pragma once`.
- Namespaces: everything under `namespace rpg_os`; **no** `using namespace` in
  headers.
- Naming (LLVM-flavoured, matching `.clang-format`):
  - Types / classes / templates: `PascalCase` (`RuleEngine`)
  - Functions / variables: `camelCase` (`evaluateRule`, `activeRules`)
  - Constants / macros: `SCREAMING_SNAKE_CASE`
  - Member variables: `m_camelCase` (`m_engine`)
- Public API in headers gets Doxygen comments (`///` or `/** */`) describing
  purpose, parameters, and return value.
- Error handling: exceptions or `std::expected` (C++23); avoid raw error codes
  where a type is clearer.
- Keep functions short and focused; mind `-Wconversion` / `-Wsign-conversion`.
- Generated headers: `// AUTOMATICALLY GENERATED BY RPG OS CODEGEN — DO NOT
  HAND EDIT`, produced by `codegen/rpg_os_codegen.py`, clang-format clean.

## Definition of done

- [ ] Test written first (TDD red) and passing (green) for the change
- [ ] `clang-format -i` applied to all touched files (not `third_party/`)
- [ ] No new clang-tidy or compiler warnings
- [ ] `cmake --build build` succeeds
- [ ] `ctest` passes — the full suite including parity and codegen-output tests
- [ ] Ruleset JSON changes: regenerated `generated/*.hpp` are in sync (`git diff` clean after codegen)
- [ ] Ruleset JSON changes: `python3 codegen/validate_ruleset.py rulesets/*.json` reports OK
