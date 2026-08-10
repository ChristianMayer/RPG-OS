# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.24)

# --- Version ---------------------------------------------------------------
# Single source of truth: the root VERSION file (e.g. "develop" on the develop
# branch, a release number like "0.1.0" on main / release tags). The Doxyfile's
# PROJECT_NUMBER reads the same file via $RPG_OS_VERSION.
#
# project() deliberately omits VERSION: the file may hold non-semver values
# such as "develop", which CMake's project(VERSION) would reject. Wire it into
# project()/packaging later, once the API is stable and versions are numeric.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _rpg_os_version_raw)
string(STRIP "${_rpg_os_version_raw}" RPG_OS_VERSION)

project(rpg_os
  DESCRIPTION "Universal Rule Engine for Role-Playing Games"
  HOMEPAGE_URL "https://github.com/Mundus-Mirabilis/RPG-OS"
  LANGUAGES CXX)

# --------------------------------------------------------------------------
# Modern C++ settings
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep binaries out of the source tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(RPG_OS_BUILD_TESTS        "Build the test suite"             ON)
option(RPG_OS_BUILD_EXAMPLES     "Build the example programs"        ON)
option(RPG_OS_ENABLE_SANITIZERS  "Build with ASan + UBSan"          OFF)
option(RPG_OS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(RPG_OS_RUN_CODEGEN        "Regenerate generated/*.hpp on build" OFF)
option(RPG_OS_BUILD_DOCS         "Generate the Doxygen HTML documentation" ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Doxygen.cmake)

# --------------------------------------------------------------------------
# Library target — header-only.
#
# rpg_os is an INTERFACE (header-only) target: all code lives under
# include/rpg_os/. Do NOT convert it to STATIC — only tests and examples
# compile executables.
#
# Vendored single-header dependencies (nlohmann/json, doctest) live in
# include/rpg_os/third_party/ and are exposed as a SYSTEM include directory so
# the library, generated code, and tests can include them without triggering
# compiler warnings.
# --------------------------------------------------------------------------
add_library(rpg_os INTERFACE)
add_library(rpg_os::rpg_os ALIAS rpg_os)

target_compile_features(rpg_os INTERFACE cxx_std_23)
target_include_directories(rpg_os INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/generated>
  $<INSTALL_INTERFACE:include>)
target_include_directories(rpg_os SYSTEM INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/rpg_os/third_party>
  $<INSTALL_INTERFACE:include/rpg_os/third_party>)

# Packaging / install rules go here once the API is stable, e.g.:
#   include(GNUInstallDirs)
#   install(TARGETS rpg_os EXPORT rpg_osTargets
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
#   install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# --------------------------------------------------------------------------
# Codegen
#
# Regenerates generated/*.hpp from the rulesets with codegen/rpg_os_codegen.py.
# Run manually with `cmake --build build --target rpg_os_codegen`, or pass
# -DRPG_OS_RUN_CODEGEN=ON to make it part of the default build. Generated
# headers are committed; keep them in sync with the rulesets (CI verifies).
# --------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_custom_target(rpg_os_codegen
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/codegen/rpg_os_codegen.py
            --ruleset ${PROJECT_SOURCE_DIR}/rulesets/dnd5e_srd.json
            --out ${PROJECT_SOURCE_DIR}/generated
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/codegen/rpg_os_codegen.py
            --ruleset ${PROJECT_SOURCE_DIR}/rulesets/tde5e_core.json
            --out ${PROJECT_SOURCE_DIR}/generated
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/codegen/rpg_os_codegen.py
            --ruleset ${PROJECT_SOURCE_DIR}/rulesets/brp_ugc.json
            --out ${PROJECT_SOURCE_DIR}/generated
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Regenerating codegen outputs from the rulesets")
  if(RPG_OS_RUN_CODEGEN)
    add_custom_target(rpg_os_codegen_all ALL DEPENDS rpg_os_codegen)
  endif()
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(RPG_OS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------
if(RPG_OS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
