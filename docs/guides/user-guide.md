\page user_guide User Guide

*Build a game, a tool, or a virtual tabletop on top of RPG OS.* This guide is
for everyone who wants to **use** the library — game designers, developers
writing digital companions, web apps or whole VTTs. It tells you what you
actually need from the API reference and points you at the right entry points.
If you want to work *on* the engine itself, see the
\ref developer_guide "Developer Guide".

## The two modes — pick your path

RPG OS reads **ruleset JSON** files and interprets them; it has no built-in
knowledge of any game. You use it in one of two ways:

1. **Universal mode** — the dynamic engine. One header, loads any ruleset at
   runtime. Best for tools, editors, web builds, and getting started.
2. **Specific mode** — generated, strongly typed code. A code generator
   compiles a ruleset's schema into a C++ header with named members and
   methods. Best for performance-critical, compile-time-checked game code.

Both modes share the same core, so results are identical. You can start in
universal mode and switch to specific mode later without changing behaviour.

## Universal mode

The entry point is the `RulesetEngine` class
(`include/rpg_os/universal/engine.hpp`). It owns everything: the loaded
ruleset, entity creation, check resolution, combat and the event bus.

```cpp
#include <rpg_os/universal/engine.hpp>
#include <iostream>

int main() {
  rpg_os::RulesetEngine engine;
  engine.loadRulesetFromFile("rulesets/dnd5e_srd.json");   // any ruleset JSON

  // Create a creature from a named bestiary entry.
  auto goblin = engine.createCreature("goblin_warrior");

  // Named entities (archetypes) and resources work the same way.
  auto geron = engine.createEntity("geron");     // TDE archetype
  std::cout << goblin.getStat("COU") << " " << geron.getResource("LP") << "\n";

  // Resolve a named check from the ruleset's check_types.
  rpg_os::CheckParams params;
  rpg_os::DefaultRandom rng;
  const rpg_os::CheckResult result = engine.executeSkillCheck("Climbing", geron, params, rng);
  std::cout << "Climbing: " << result.total << "\n";
}
```

What you will use most from the API reference:

| Class / type | Header | What it is |
| --- | --- | --- |
| `RulesetEngine` | `universal/engine.hpp` | The facade: load rulesets, create entities, run checks, combat, spells, bookkeeping |
| `DynamicEntity` | `universal/dynamic_entity.hpp` | A character sheet at runtime: stats, resources, inventory, conditions |
| `CheckResult` / `CheckParams` | `core/checks.hpp` | Inputs and results of a check resolution |
| `DefaultRandom` | `core/dice_engine.hpp` | The injectable random number generator (seed it for reproducible runs) |
| `DiceExpression` | `core/dice_engine.hpp` | Dice expressions, e.g. `2_d6 + 4` |
| `Variance` | `core/variance.hpp` | Range selection: weakest … strongest |
| `GameSession` | `universal/game_session.hpp` | A whole campaign state: many entities, world state, save/load |
| `CombatSession` | `universal/combat_session.hpp` | Initiative, turns, action budget |

The engine's methods mirror a rulebook: `createEntity`, `createCreature`,
`executeCheck`, `executeSkillCheck`, `castSpell`, `applyAffliction`, `runFight`,
`buy`, `equip`, `gainXp`, `longRest`, … Search the reference for the operation
you need — if a rulebook covers it, the engine most likely models it as data.

## Specific mode (generated code)

Run the code generator on a ruleset to produce a strongly typed header:

```sh
python3 codegen/rpg_os_codegen.py --ruleset rulesets/tde5e_core.json --out generated/
```

The generated `generated/tde5e_core_static.hpp` declares a `Character` class
with named members and methods — attributes become members, derived stats
become compiled getters, checks become named methods:

```cpp
#include <rpg_os/common/json.hpp>
#include <rpg_os/core/dice_engine.hpp>
#include <tde5e_core_static.hpp>          // generated

const rpg_os::Json ruleset = rpg_os::Json::parse(file_contents);
const auto geron = rpg_os::generated::tde5e::Character::fromArchetype(ruleset, "geron");

std::cout << geron.courage();             // 12
std::cout << geron.resource("LP");        // 31
const auto climb = geron.checkClimbing(rpg_os::CheckParams{}, rng);
```

Generated code still loads the ruleset JSON at **runtime** for the data
database (creatures, items, archetypes); only the *rules* are compiled. The
generated headers depend only on `rpg_os/common` and `rpg_os/core` — not on
the universal layer.

## The examples

The `examples/` directory contains small, self-contained programs that
demonstrate both modes and the surrounding patterns. Build and run them to see
RPG OS in action:

```sh
cmake --build build
./build/bin/rpg_os_example_universal .    # dynamic engine
./build/bin/rpg_os_example_specific .     # generated, strongly typed code
./build/bin/rpg_os_example_fight .        # fight two combatants to the end
./build/bin/rpg_os_example_fight . magister toad   # a mage fights with magic
./build/bin/rpg_os_example_ecs_registry . # entity-component-system pattern, no deps
./build/bin/rpg_os_example_ecs_entt .     # the same pattern with EnTT
```

See `examples/README.md` for the full list and CLI options.

## Creating your own ruleset JSON

A ruleset is **one JSON file**. This is how you add a new game system — you
never touch the engine. The file is validated against
[`rulesets/ruleset.schema.json`](../rulesets/ruleset.schema.json) (JSON
Schema draft-07).

The minimal skeleton looks like this:

```jsonc
{
  "schema_version": 1,
  "ruleset_id": "my_rpg",
  "ruleset_name": "My RPG",
  "source": "…",
  "licence": "CC-BY-4.0",            // required, non-empty — never assume Apache-2.0
  "namespace": "rpg_os::generated::my_rpg",
  "attributes": [
    { "id": "STR", "name": "Strength", "default": 10 },
    { "id": "DEX", "name": "Dexterity", "default": 10 }
  ],
  "derived_stats": [
    { "id": "HitPoints_Max", "name": "Hit Points", "formula": "10 + STR" }
  ],
  "resource_pools": [
    { "id": "HP", "name": "Hit Points", "max_stat": "HitPoints_Max" }
  ],
  "check_types": [
    { "id": "climbing", "name": "Climbing", "resolution": "threshold",
      "dice": "1d20", "comparison": "ge", "threshold_source": "difficulty" }
  ],
  "skills": [],
  "cost_tables": [],
  "equipment_slots": [],
  "event_triggers": [],
  "data": { "creatures": [], "spells": [], "items": [], "conditions": [], "traits": [] }
}
```

Check it with the shipped validator before using it:

```sh
python3 codegen/validate_ruleset.py rulesets/my_rpg.json
```

A few things worth knowing about the format:

- **Numeric ranges** may be a plain integer (`7`), a dice string (`"2d6+4"`),
  or an object (`{ "min": 2, "max": 12 }`). Ranges are resolved per
  `Variance` (weakest … strongest) at call time — a range is never collapsed
  to a single number.
- **Formulas** use a restricted grammar (`+ - * / % ^`, comparisons,
  `&& || !`, `min max floor ceil round clamp`) that the code generator can
  translate to C++. They may reference attributes, `actor.*`, `target.*`,
  `env.*`, and parameters.
- **Checks** are generic, data-driven `CheckRecipe`s — resolution
  (threshold / pool / opposed / resistance), dice, comparison, criticals,
  fumbles, grading, difficulty. A new combination of dice and comparison is a
  JSON value, not an engine change.
- **Optional sections** are opt-in: `currencies`, `encumbrance`,
  `spellcasting`, plus `data.traits` for always-on creature abilities.
- **Licences are per-ruleset.** The `licence` field is required; add
  `licence_source` (a URL to where the rights holder states it),
  `licence_notice` (verbatim text a licence requires, e.g. the ORC Notice)
  and `attribution` (required credit) when they apply. The engine refuses to
  load a ruleset without a licence.

Look at `rulesets/tde5e_core.json` (small and complete) and
`rulesets/dnd5e_srd.json` (large) as real-world reference files.

## On the web — WASM and npm

The universal engine is compiled to WebAssembly and published as
`@mundus-mirabilis/rpg-os` — see the package's
[README](https://github.com/Mundus-Mirabilis/RPG-OS/blob/develop/wasm/package/README.md). It runs in Node.js and in the browser,
with any ruleset JSON loaded at runtime. The ELO Arena live demo
(<https://mundus-mirabilis.github.io/RPG-OS/main/demo/>) is the fastest way
to see what the engine can do without compiling anything.

## Where to go next in the reference

- **Classes** tab — the full list of types; start with `RulesetEngine` and
  `DynamicEntity`.
- **Files** tab — every header, grouped by `common/`, `core/`, `universal/`,
  `specific/`, `generated/`.
- **Namespaces** tab — everything lives under `rpg_os` (generated code adds
  `rpg_os::generated::<ruleset>`).

If you only remember two things: **`RulesetEngine` is the entry point in
universal mode**, and **the ruleset JSON is your game system — the engine
just interprets it**.

\defgroup rpg_os_specific Specific mode — generated, strongly typed code
Group for the generated, strongly typed code headers (`generated/*_static.hpp`)
and their runtime support. See the \ref user_guide "User Guide" for how to
use them; each generated header is added to this group by the code generator.
