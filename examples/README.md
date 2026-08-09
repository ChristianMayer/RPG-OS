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

Build:

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/bin/rpg_os_example_universal .
./build/bin/rpg_os_example_specific .
```

Not part of the library itself and not installed.
