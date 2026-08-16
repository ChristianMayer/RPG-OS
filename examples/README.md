# Examples

Small, self-contained programs that demonstrate the `rpg_os` library on the
shipped rulesets. Each example lives in its own subdirectory and is registered
as an executable in the build.

- `universal_mode/` — the dynamic engine: loads any ruleset JSON at runtime and
  resolves rules generically (derived stats, named checks, DSA skill checks,
  D&D attacks).
- `specific_mode/` — the code-generated headers: named members, compiled
  derived-stat getters, and named check methods, while the character data is
  still read from the ruleset JSON at runtime.
- `fight/` — fight two archetypes or bestiary entries to the end and announce
  the winner (CLI-selectable combatants, `--csv` output for scripts). Built on
  the combat helpers in `include/rpg_os/universal/combat.hpp`; a Monte Carlo
  ELO ranking that runs many fights in parallel lives in
  `scripts/elo_ranking.py`. A spellcaster (e.g. the TDE Magister) fights with
  magic by default; `--no-magic` forces a pure weapon fight.
- `ecs_registry/` — the entity-component-system **pattern** with no third-party
  dependency: entities are live `DynamicEntity` sheets addressed by a stable
  `EntityId`/`EntityHandle`, components are plain id-keyed structs, and systems
  are plain functions over `GameSession::entities()`. Shows the observer
  events (`OnResourceChanged`) driving an HP-bar component and a
  `CombatSession` decaying a timed condition across turns.
- `ecs_entt/` — the same idea with **EnTT** (vendored single header) as the
  storage layer: each EnTT entity carries a `Sheet` component owning a
  `DynamicEntity`, and an `OnResourceChanged` listener updates an EnTT
  `HpBar` component without polling.

Build:

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/bin/rpg_os_example_universal .
./build/bin/rpg_os_example_specific .
./build/bin/rpg_os_example_fight .                 # default pair
./build/bin/rpg_os_example_fight . irrhalk dog     # named pair
./build/bin/rpg_os_example_fight . --list          # all combatants
./build/bin/rpg_os_example_fight . --csv --batch 20 irrhalk toad
./build/bin/rpg_os_example_fight . magister toad   # a mage fights with magic
./build/bin/rpg_os_example_ecs_registry .          # ECS pattern, no deps
./build/bin/rpg_os_example_ecs_entt .              # ECS pattern with EnTT
```

Not part of the library itself and not installed.
