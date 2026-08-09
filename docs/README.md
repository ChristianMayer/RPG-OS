# Documentation

RPG-OS ships generated API documentation rendered with
[Doxygen](https://www.doxygen.nl/) and the
[Doxygen Awesome](https://github.com/jothepro/doxygen-awesome-css) theme
(vendored under [`docs/doxygen-awesome/`](doxygen-awesome/)).

## Published site

Versioned builds are published to GitHub Pages:

| URL | Content |
| --- | --- |
| `https://Mundus-Mirabilis.github.io/RPG-OS/` | Root redirect to the `main` build |
| `https://Mundus-Mirabilis.github.io/RPG-OS/main/` | Docs for the `main` branch |
| `https://Mundus-Mirabilis.github.io/RPG-OS/develop/` | Docs for the `develop` branch |
| `https://Mundus-Mirabilis.github.io/RPG-OS/vX.Y.Z/` | Docs for release tag `vX.Y.Z` |

All versions live side-by-side in subfolders of the `gh-pages` branch
(`keep_files: true`), so an older release's docs stay reachable when a newer
one is deployed.

## How the docs are built

The single source of truth is the **`Doxyfile` at the repository root**. It
uses paths relative to the repository root and renders into **`html/`** — a
gitignored directory in the source tree, so generated pages are never
committed.

- **Locally (VS Code):** the **"Build Documentation"** task
  (`.vscode/tasks.json`) runs the `rpg_os_docs` CMake target:
  `cmake --build build --target rpg_os_docs` → `./html`.
- **Locally (terminal / any IDE):** same CMake target works everywhere; with
  `RPG_OS_BUILD_DOCS=ON` (default) it is part of the default build.
- **CI:** `.github/workflows/docs.yml` calls the central reusable
  `doxygen-deploy` workflow (`Mundus-Mirabilis/.github`) with
  `doxyfile-path: Doxyfile` and `output-dir: html`. That workflow runs
  Doxygen from the repo root, then deploys `html/` to the `gh-pages` branch
  under a subfolder named after the ref (`main`, `develop`, or the release
  tag).

## Version shown in the docs

The version displayed in the docs header (`PROJECT_NUMBER`) has a **single
source of truth: the `VERSION` file at the repository root**. It holds e.g.
`develop` on the develop branch or a release number like `0.1.0` on `main` /
release tags. The `Doxyfile` references it as `PROJECT_NUMBER =
"$(RPG_OS_VERSION)"` (an environment variable that Doxygen expands), and the
value is injected by:

- **Locally:** `cmake/Doxygen.cmake` reads `VERSION` and passes it as the
  `RPG_OS_VERSION` environment variable to the `rpg_os_docs` target.
- **CI:** the central `doxygen-deploy` workflow reads `VERSION` into
  `RPG_OS_VERSION` before running Doxygen.

Do **not** hardcode a version in `CMakeLists.txt` or the `Doxyfile` — update
`VERSION` instead. (For this reason `project()` in `CMakeLists.txt` omits
`VERSION`: the file may hold non-semver values such as `develop`.)

## One-time GitHub repository settings

The deployment workflow needs:

1. **Settings → Actions → General → Workflow permissions:** *Read and write
   permissions* (so the workflow can push the `gh-pages` branch).
2. **Settings → Pages → Source:** *Deploy from a branch* → branch `gh-pages`,
   folder `/ (root)`. The `gh-pages` branch is auto-created by the action on
   its first run.

Fork pull requests build the docs as a smoke check, but deployment is skipped
automatically (forks have no write access to the upstream `gh-pages` branch).

## Requirements

- [Doxygen](https://www.doxygen.nl/download.html) ≥ 1.9 (the CMake target is
  skipped gracefully when Doxygen is not found)
- [Graphviz](https://graphviz.org/) (`dot`) for the class / include / hierarchy
  diagrams (optional — set `HAVE_DOT = NO` in the `Doxyfile` to build without
  it)

## How the docs are structured

- `README.md` (the repository root readme) is the front page
  (`USE_MDFILE_AS_MAINPAGE`).
- All public headers under `include/rpg_os/` are documented with Doxygen
  comments — including the "why" behind each design decision (see the
  project's documentation conventions in `AGENTS.md`).
- The example programs under `examples/` and the test suite under `tests/` are
  included so the documentation covers the whole codebase, not just the API.
- The committed, generated specific-mode headers under `generated/` are
  included too — they document the strongly typed `Character` classes.
- **Internal documents are excluded**: `AGENTS.md`, `CONTRIBUTING.md` and
  similar contributor-facing files are deliberately *not* part of the Doxygen
  input, so the published pages only contain what a library user needs.
- Vendored third-party headers (`include/rpg_os/third_party/`) are excluded:
  they are documented by their own projects.
- Branding assets (logo, banner, favicon) live in `docs/assets/` and are
  referenced from the `Doxyfile`, `header.html`, and `README.md` — see
  `docs/assets/README.md` for the format conventions and how to replace them.
- The theme (stylesheets, JS, `header.html`, `custom.css`) is vendored under
  `docs/doxygen-awesome/`; see its `README.md` for provenance and the upgrade
  procedure.
