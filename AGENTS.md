# AGENTS.md

Instructions for LLM coding agents (and human contributors) working in this
repository. **Read and follow these rules for every change you make.**

## Project

RPG-OS — a **C++23 library**: a universal rule engine for role-playing games.
The concrete API and content are not defined yet; when implementing features,
keep them consistent with the structure described here and the existing files.

## Ground rules

1. **Style is enforced — do not deviate.** Run `clang-format -i <file>` on
   every file you touch (config: `.clang-format`).
   - Indentation (shift width): **2 spaces**
   - Tab width: **8 columns**; use **spaces only**, never tab characters
   - Column limit: **100**
2. **Keep clang-tidy clean.** `.clang-tidy` enables `modernize-*`,
   `cppcoreguidelines-*`, `performance-*`, etc. Do not introduce new warnings;
   fix them before finishing.
3. **Modern C++23 only.** Prefer:
   - `const` / `constexpr` where possible, `noexcept` where appropriate
   - value semantics and smart pointers — no manual `new`/`delete`
   - structured bindings, `std::ranges`, concepts where they improve clarity
   - `std::array`/`std::vector` over C arrays, `static_cast`/`reinterpret_cast`
     over C-style casts
   - `std::string_view`/`std::span` for non-owning parameters
4. **Don't break the build.** Configure, build, and test before finishing
   (commands below).

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:
- `-DRPGOS_WARNINGS_AS_ERRORS=ON` — treat warnings as errors (enforced in CI)
- `-DRPGOS_ENABLE_SANITIZERS=ON` — ASan + UBSan
- `-DRPGOS_BUILD_TESTS=OFF` — skip the test suite

## Project layout

| Path | Purpose |
| --- | --- |
| `include/rpgos/` | Public headers (the installed API) — one header per module, e.g. `include/rpgos/engine.hpp` |
| `src/` | Implementation (`.cpp`) files |
| `src/internal/` | Private helpers that must never leak into public headers |
| `tests/` | Test suite, registered with CTest via `tests/CMakeLists.txt` |
| `examples/` | Small self-contained demo programs |
| `cmake/` | CMake helper modules (warnings, sanitizers) |

When adding the first source file, change the placeholder `rpgos` target in
`CMakeLists.txt` from `INTERFACE` to `STATIC` and list the sources there.

## Code conventions

- Header guards: `#pragma once`.
- Namespaces: everything under `namespace rpgos`; **no** `using namespace` in
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

## Definition of done

- [ ] `clang-format -i` applied to all touched files
- [ ] No new clang-tidy or compiler warnings
- [ ] `cmake --build build` succeeds
- [ ] `ctest` passes — add tests for new functionality
