# RPG-OS

A **header-only, C++23** universal rule engine and "operating system" for
tabletop role-playing games. All rules live in ruleset-specific JSON files;
the engine resolves them in two modes that share one template-based core.

- **Universal mode (dynamic):** load any ruleset JSON at runtime and resolve
  rules generically — attribute/derived-stat calculation, d20 / 3d20 check
  resolution, modifier pipelines, cost tables, and event triggers. New game
  systems are added by writing JSON, never by recompiling.
- **Specific mode (codegen):** `codegen/rpg_os_codegen.py` compiles a ruleset's
  schema into a strongly typed header with named members and methods
  (`courage()`, `baseAttack()`, `climbCheck()`, ...). Formulas compile to C++
  and checks use `constexpr` configs — no dynamic allocation in the hot path.
  The generated code still loads the JSON at runtime for the data database
  (creatures, items, archetypes).

![RPG-OS — a header-only C++23 universal rule engine for tabletop RPGs](docs/assets/mundus-mirabilis-RPG_OS.png)

Full example rulesets are provided for **D&D 5th Edition (SRD 5.2.1)** and
**The Dark Eye 5th Edition** in `rulesets/`. Their `data` sections are
complete transcriptions of the source documents: the D&D SRD ships the full
bestiary (317 monsters), spell list (329 spells), all 15 conditions, all 14
sample poisons and 3 magical contagions; the TDE core rules ship its complete
bestiary (10 creatures), spell list (52 spells), statuses, and poisons. The
extraction is reproducible via `.local_ressources/extract_srd_data.py` and
`.local_ressources/extract_tde_data.py` (they parse the PDF-extracted markdown
in `.local_ressources/`; these one-off helpers are kept out of the repository).

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:

| Option | Default | Effect |
| --- | --- | --- |
| `RPG_OS_BUILD_TESTS` | `ON` | Build the test suite |
| `RPG_OS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors |
| `RPG_OS_ENABLE_SANITIZERS` | `OFF` | Build with AddressSanitizer + UBSan |
| `RPG_OS_RUN_CODEGEN` | `OFF` | Regenerate `generated/*.hpp` during the build |
| `RPG_OS_BUILD_DOCS` | `ON` | Render the Doxygen documentation into `html/` (gitignored) |

Requires CMake ≥ 3.24 and a C++23-capable toolchain whose standard library
provides `<expected>`: GCC ≥ 13 (libstdc++ ≥ 13), or Clang ≥ 17 with libc++ ≥ 17
(Apple clang on macOS, or `-stdlib=libc++` on Linux). Clang 17/18 with GNU
libstdc++ 13 does not provide `<expected>` — use Clang ≥ 19 with libstdc++
instead.

## Documentation

The **API reference** is rendered with [Doxygen](https://www.doxygen.nl/) and
the [Doxygen Awesome](https://github.com/jothepro/doxygen-awesome-css) theme
(vendored under `docs/doxygen-awesome/`). It is published on GitHub Pages for
both lines of development:

| URL | Content |
| --- | --- |
| <https://mundus-mirabilis.github.io/RPG-OS/develop/> | **Current development** — the docs for the `develop` branch |
| <https://mundus-mirabilis.github.io/RPG-OS/main/> | **Latest release** — the docs for the `main` branch |

Release tags are additionally deployed under `/vX.Y.Z/`. Every build is
rendered from the same `Doxyfile` in this repository, so the local and the
published documentation are identical.

Documentation generation is part of the build (`RPG_OS_BUILD_DOCS=ON` by
default) and can also be triggered from VS Code via the
**"Build Documentation"** task:

```sh
cmake --build build --target rpg_os_docs   # HTML -> ./html (gitignored)
```

The generated pages live in `html/` at the repository root — a gitignored
directory in the source tree, never committed. The Doxygen configuration is
the single `Doxyfile` at the repository root, used identically by the local
build and CI. See `docs/README.md` for the full setup.

Regenerating the codegen outputs:

```sh
python3 codegen/rpg_os_codegen.py --ruleset rulesets/dnd5e_srd.json --out generated/
python3 codegen/rpg_os_codegen.py --ruleset rulesets/tde5e_core.json --out generated/
```

## npm package & live demo

The universal engine is also compiled to **WebAssembly** and published as the
npm package **`@mundus-mirabilis/rpg-os`** — usable in **Node.js** and in the
**browser**, with **any** ruleset JSON. The WASM binary never embeds a game's
data: Node reads the JSON with `fs`, browsers fetch it, and both hand the
string to the engine at runtime.

```js
import { init } from '@mundus-mirabilis/rpg-os';

const rpg = await init();                                  // loads the WASM engine
rpg.loadRulesetFromFile('rulesets/tde5e_core.json');       // Node: native fs
// browser: rpg.loadRuleset(await (await fetch('rulesets/...')).text());

const geron = rpg.createEntity('geron');                   // from a named archetype
console.log(geron.getStat('COU'), geron.getResource('LP'));

const hero = rpg.createEntityFromSheet('my_hero', { COU: 14, Attack: 12 }); // ANY character
const spec = rpg.specFromEntity(hero);
const outcome = rpg.fight(spec, rpg.specFromId('toad'), { seed: 42 });      // loop for win %
```

The package is built and published by CI: every push to `main` and `develop`
updates a branch snapshot, and every `v*` tag publishes the release under
`latest`. Consumers pick a line of development explicitly —
`npm i @mundus-mirabilis/rpg-os` (release), `@main` or `@develop` (snapshots).
See `wasm/` (bindings + build script) and `wasm/package/` (the package) for
details, and `wasm/package/test/` for the test suite — including the seeded
**native-vs-WASM fight parity test**.

**ELO Arena** — a live demo, deployed alongside the docs to `/<version>/demo/`
(e.g. <https://mundus-mirabilis.github.io/RPG-OS/main/demo/>), shows the ELO
ranking of every combatant in a ruleset (generated during CI by
`scripts/elo_ranking.py`), lets you enter an arbitrary character that is
ranked live in the browser via WASM, and computes head-to-head fight-win
probabilities from the ELO ratings. The ELO ratings stay in Python — the page
only ports the small formula to JS for the interactive, serverless parts.
Source in `web/demo/`; the demo is deployed by the `deploy-demo` job of
`.github/workflows/docs.yml` (chained after the docs deploy, so the two
gh-pages pushes never race).

## Using the generated (specific-mode) headers

The code generator compiles a ruleset's schema into a strongly typed header.
Attributes become named members, derived stats become compiled getters, and
checks become named methods — while the character data still comes from the
ruleset JSON at runtime:

```cpp
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <tde5e_core_static.hpp>   // generated by rpg_os_codegen.py

const rpg_os::Json ruleset = rpg_os::Json::parse(file_contents);
const auto geron = rpg_os::generated::tde5e::Character::fromArchetype(ruleset, "geron");

rpg_os::CheckParams params;
rpg_os::DefaultRandom rng;
const rpg_os::CheckResult climb = geron.checkClimbing(params, rng); // 3d20 vs COU/AGI/STR
```

The same check through the universal engine produces identical results — a
parity test enforces this.

## Ruleset format, licence, and schema

Every ruleset is a single JSON file validated against
[`rulesets/ruleset.schema.json`](rulesets/ruleset.schema.json) (JSON Schema
draft-07). The top level carries the ruleset metadata, including two fields
every ruleset must provide:

```jsonc
{
  "schema_version": 1,
  "ruleset_id": "dnd5e_srd",
  "ruleset_name": "Dungeons & Dragons 5th Edition (SRD 5.2.1)",
  "source": "System Reference Document 5.2.1, Wizards of the Coast (CC-BY-4.0)",
  "licence": "CC-BY-4.0",          // required, non-empty
  "comment": "...",                 // optional free-form note
  "namespace": "rpg_os::generated::dnd5e",
  "attributes": [ /* ... */ ],
  "data": { /* creatures, spells, conditions, poisons, diseases, ... */ }
}
```

The loader refuses to load a ruleset without a `licence`, and the shipped
validator `codegen/validate_ruleset.py` checks every ruleset against the
schema (full draft-07 validation when `jsonschema` is installed, structural
fallback otherwise). CI runs it on every pull request.

## Ranges and variance (weakest … strongest)

Data records (creatures, archetypes, items, spells) may express a numeric
value in three forms:

- a plain integer: `7`,
- a dice expression string: `"2d6+4"` (kept verbatim from the source),
- an explicit range object: `{ "min": 2, "max": 12 }`.

Once a dice string is parsed it is a plain C++ object, and there are two
literal forms that build one directly from source text:

```cpp
using namespace rpg_os::dice_literals; // brings in the die-size suffixes

auto attack = 1_d20;         // one D20
auto damage = 2_d6 + 2;      // two D6 plus two
auto heavy  = 3_d6 - 1_d4;   // combined groups and subtraction
auto weird  = "1d7+1d23"_dice; // unusual sizes (any ruleset dice string)
```

The `_dN` suffixes (`_d2`, `_d3`, `_d4`, `_d6`, `_d8`, `_d10`, `_d12`,
`_d20`, `_d30`, `_d100`) make the common cases readable, with `+`/`-`
overloads on
`rpg_os::DiceExpression` folding the result into one object; the `_dice`
suffix parses any dice string — the form the generated ruleset headers use,
so a check's dice are parsed once at startup rather than on every roll.

When a caller picks such an entry (e.g. an animal as a fight opponent) it can
request a **variance** via `rpg_os::Variance`:

| Variance | Meaning |
| --- | --- |
| `Random` (default) | uniform over the range; dice expressions are rolled |
| `Weakest` | the minimum |
| `Weak` | random value in the lower third |
| `Average` | random value in the middle third |
| `Strong` | random value in the upper third |
| `Strongest` | the maximum |

```cpp
// Universal mode: create the goblin with minimum hit points.
auto weak = engine.createCreature("goblin_warrior", rpg_os::Variance::Weakest);

// Specific mode: same selection on the generated, strongly typed character.
auto monster = rpg_os::generated::dnd5e::Character::fromCreature(
    ruleset, "goblin_warrior", rpg_os::Variance::Weakest, rng);
```

`Variance::Weak` / `Average` / `Strong` draw from the lower / middle / upper
third of the range at random; `Random` draws uniformly over the full range.
The shared helper `rpg_os::readVariantValue` (`include/rpg_os/core/variance.hpp`)
backs both the universal engine and the generated code.

## Project layout

```
.
├── .github/workflows/ci.yml    # CI: GCC + Clang on Ubuntu/macOS + codegen sync check
├── .github/workflows/docs.yml  # Builds the docs and deploys them to GitHub Pages
├── cmake/                      # CMake helper modules (warnings, sanitizers, docs)
├── docs/                       # Doxygen config + vendored Doxygen Awesome theme
├── include/rpg_os/
│   ├── common/                # shared value types, JSON alias, event system
│   ├── core/                  # shared header-only template core (dice, math,
│   │                          #   modifier, cost tables, checks, entity)
│   ├── universal/             # dynamic mode: loader, AST evaluator, engine
│   ├── specific/              # runtime support for generated code
│   └── third_party/           # vendored headers (nlohmann/json, doctest)
├── src/                       # reserved (library is header-only)
├── codegen/                   # rpg_os_codegen.py + validate_ruleset.py
├── scripts/                   # elo_ranking.py (Monte Carlo ELO tournament)
├── rulesets/                  # dnd5e_srd.json, tde5e_core.json, ruleset.schema.json
├── generated/                 # committed codegen outputs
├── tests/                     # doctest suite
└── examples/                  # demo programs
```

## Combat simulation & Monte Carlo ELO ranking

`include/rpg_os/universal/combat.hpp` runs a fight between two archetypes or
bestiary entries "to the end" (attack-vs-defence check, event-driven damage
pipeline with armour absorption), creating fresh entities per fight so ranged
values are re-rolled every time. It is demonstrated by the `fight` example:

```sh
cmake --build build --target rpg_os_example_fight
./build/bin/rpg_os_example_fight .              # default pair (Geron vs Gotongi)
./build/bin/rpg_os_example_fight . irrhalk dog  # pick two combatants by id
./build/bin/rpg_os_example_fight . --list       # all combatants in the ruleset
```

A combatant that knows spells (an archetype with `spells_known`, e.g. the TDE
Magister) fights with **magic by default**: each round it casts its strongest
affordable damaging spell instead of swinging a weapon, falling back to a
weapon only once it is out of usable magic. Pass `--no-magic` for a pure
weapon-vs-weapon comparison:

```sh
./build/bin/rpg_os_example_fight . magister toad        # magic (default)
./build/bin/rpg_os_example_fight . --no-magic magister toad
```

`scripts/elo_ranking.py` ranks every combatant with a Monte Carlo ELO
tournament. It runs a **Swiss** pairing (each combatant plays one similar-rated
opponent per round) instead of a full round-robin, so it needs only
`O(rounds · n)` fights rather than `O(n²)` — and the individual fights execute
in `--jobs` parallel processes:

```sh
python3 scripts/elo_ranking.py --jobs 8 --rounds 40 --games 10
python3 scripts/elo_ranking.py --no-magic  # rank physical combat only
```

Every random consumer follows the same rule: **explicitly seeded = exactly
reproducible, unseeded = varied on every run.** `rpg_os::randomSeed()` draws
one word of easily available entropy from the OS random device (falling back
to the high-resolution clock where no device exists) — plenty for a game,
where the goal is simply that no two runs are the same. A default-constructed
`rpg_os::DefaultRandom` seeds its `std::mt19937` from it, so `--seed N`
replays a fight or ranking exactly, while omitting `--seed` gives a different
result on every run (the ELO script prints the seed it used so a run can be
reproduced by re-passing it). Callers who need stronger randomness can pass an
explicit seed of their own.
