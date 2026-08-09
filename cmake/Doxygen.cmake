# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

# --------------------------------------------------------------------------
# Doxygen documentation (RPG-OS + the Doxygen Awesome theme).
#
# Adds the `rpg_os_docs` custom target, which renders the HTML documentation
# into <repo-root>/html — a gitignored directory in the source tree — using
# the committed Doxyfile at the repository root.
#
# Why a source-tree directory? Local docs are built through this target (the
# "Build Documentation" VS Code task is a thin wrapper around it, and other
# IDEs/terminals can call the very same CMake target), and the generated pages
# must live in a gitignored folder that sits next to the source. CI renders
# the same Doxyfile via the central reusable workflow (see
# .github/workflows/docs.yml), so local and published docs are identical.
#
# Behaviour:
#   - When Doxygen is not found the target is silently skipped (documentation
#     is an optional, additive part of the build).
#   - With -DRPG_OS_BUILD_DOCS=ON (the default) the target is added to ALL, so
#     a plain `cmake --build build` also renders the docs.
#   - With -DRPG_OS_BUILD_DOCS=OFF the target still exists and can be built
#     on demand with `cmake --build build --target rpg_os_docs`.
# --------------------------------------------------------------------------

find_package(Doxygen)

if(NOT DOXYGEN_FOUND)
  message(STATUS "rpg_os_docs: Doxygen not found — documentation target disabled")
  return()
endif()

set(_rpg_os_docs_all)
if(RPG_OS_BUILD_DOCS)
  set(_rpg_os_docs_all ALL)
endif()

# Doxygen resolves relative paths from its working directory, so run it from
# the source root; the committed Doxyfile then emits ./html there. The output
# directory is wiped first so a rebuild never carries over stale pages from an
# older Doxyfile (Doxygen does not clean its own output; CI starts from a fresh
# checkout so it is inherently clean, this makes local builds match).
add_custom_target(rpg_os_docs ${_rpg_os_docs_all}
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PROJECT_SOURCE_DIR}/html
  COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Doxyfile
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Generating HTML documentation (Doxygen + Doxygen Awesome) -> ${PROJECT_SOURCE_DIR}/html"
  VERBATIM)

unset(_rpg_os_docs_all)
