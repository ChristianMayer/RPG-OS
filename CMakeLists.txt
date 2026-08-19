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

# --------------------------------------------------------------------------
# Required C++23 library support
#
# The bookkeeping API (engine.hpp, game_session.hpp, ...) is built on
# std::expected / std::unexpected, so a C++23 <expected> is a hard
# requirement. Not every "C++23-capable" compiler actually provides it:
#   * GCC >= 13 (libstdc++ >= 13)                -> OK
#   * Clang with libc++ >= 17                    -> OK (macOS default; on Linux
#     pass -stdlib=libc++, as the CI Clang job does)
#   * Clang >= 19 with libstdc++ >= 13           -> OK
#   * Clang 17/18 with libstdc++ 13              -> FAILS: libstdc++ 13 gates
#     <expected> behind __cpp_concepts >= 202002L, but clang 17/18 predefine
#     __cpp_concepts as 201907L, so the whole header is empty.
# Check at configure time so a misconfigured toolchain fails with a clear
# message instead of a wall of "no template named 'expected'" errors.
# --------------------------------------------------------------------------
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <expected>
int main() {
  std::expected<int, int> value{42};
  std::unexpected<int> error{1};
  (void)error;
  return value.value() != 42;
}
" RPG_OS_HAVE_STD_EXPECTED)
if(NOT RPG_OS_HAVE_STD_EXPECTED)
  message(FATAL_ERROR
    "C++23 <expected> (std::expected/std::unexpected) is required by the "
    "bookkeeping API but is not provided by this toolchain. Use GCC >= 13 "
    "(libstdc++ >= 13), or Clang with libc++ >= 17 (Apple clang on macOS, or "
    "Linux clang with -stdlib=libc++). Note that clang 17/18 with GNU libstdc++ "
    "13 does NOT work: libstdc++ 13 hides <expected> behind __cpp_concepts >= "
    "202002L, which clang 17/18 report as 201907L; use clang >= 19 with "
    "libstdc++ instead.")
endif()

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
  # The shipped rulesets. Adding a system = appending one id here; the
  # generated header name (<ruleset_id>_static.hpp) is derived by the script.
  set(RPG_OS_RULESETS dnd5e_srd tde5e_core brp_ugc)
  foreach(ruleset_id IN LISTS RPG_OS_RULESETS)
    list(APPEND _rpg_os_codegen_commands
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/codegen/rpg_os_codegen.py
              --ruleset ${PROJECT_SOURCE_DIR}/rulesets/${ruleset_id}.json
              --out ${PROJECT_SOURCE_DIR}/generated)
  endforeach()
  add_custom_target(rpg_os_codegen
    ${_rpg_os_codegen_commands}
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
