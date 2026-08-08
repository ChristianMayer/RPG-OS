# RPG-OS

Universal Rule Engine for Role-Playing Games.

## Status

Project scaffold only — no library code has been written yet. The content of
this project is still to be defined.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:

| Option | Default | Effect |
| --- | --- | --- |
| `RPGOS_BUILD_TESTS` | `ON` | Build the test suite |
| `RPGOS_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors |
| `RPGOS_ENABLE_SANITIZERS` | `OFF` | Build with AddressSanitizer + UBSan |

Requires CMake ≥ 3.24 and a C++23-capable compiler (GCC ≥ 13 or Clang ≥ 17).

## Project layout

```
.
├── .github/workflows/ci.yml   # CI: GCC + Clang on Ubuntu/macOS
├── cmake/                     # CMake helper modules (warnings, sanitizers)
├── include/rpgos/             # public headers of the rpgos library
├── src/                       # library implementation
├── tests/                     # test suite (framework TBD)
└── examples/                  # example programs (TBD)
```

## Code style

Modern C++ (C++23), formatted with the project's `.clang-format`:

- indentation (shift width): **2 spaces**
- tab width: **8** columns
- **spaces only** — tab characters are never used
- 100 column limit
- clang-tidy checks (`.clang-tidy`) enforce modern, safe C++

Run the formatter with `clang-format -i <file>` (or via clangd in VS Code).

LLM agents and contributors must follow [`AGENTS.md`](AGENTS.md) — it holds the
full style and coding rules for this repository.

