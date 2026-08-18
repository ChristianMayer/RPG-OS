# Contributing to RPG OS

Thanks for your interest in contributing! This page covers the development
workflow and the coding standards. **LLM agents** should read `AGENTS.md` —
it is the authoritative instruction file and goes into more detail than this
page.

## Development workflow

Development is **test-driven** (TDD): write a failing test first (red),
implement the minimal code to pass (green), then refactor. Never add
functionality without a test that exercises it, and keep the test suite green
before finishing a change.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful options:

- `-DRPG_OS_WARNINGS_AS_ERRORS=ON` — treat warnings as errors (enforced in CI)
- `-DRPG_OS_ENABLE_SANITIZERS=ON` — ASan + UBSan

## Code style

Modern C++ (C++23), formatted with the project's `.clang-format`:

- indentation (shift width): **2 spaces**
- tab width: **8** columns
- **spaces only** — tab characters are never used
- 100 column limit
- clang-tidy checks (`.clang-tidy`) enforce modern, safe C++

Run the formatter with `clang-format -i <file>` (or via clangd in VS Code) on
every file you touch, and keep clang-tidy clean — CI builds with warnings-as-
errors.

## Coding rules

[`AGENTS.md`](AGENTS.md) holds the full style, workflow, and coding rules for
this repository — LLM agents and human contributors must follow it before
making changes.
