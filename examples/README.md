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
  `scripts/elo_ranking.py`.

Build:

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/bin/rpg_os_example_universal .
./build/bin/rpg_os_example_specific .
./build/bin/rpg_os_example_fight .                 # default pair
./build/bin/rpg_os_example_fight . irrhalk dog     # named pair
./build/bin/rpg_os_example_fight . --list          # all combatants
./build/bin/rpg_os_example_fight . --csv --batch 20 irrhalk toad
```

Not part of the library itself and not installed.
