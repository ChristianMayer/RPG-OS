cmake_minimum_required(VERSION 3.24)

project(rpg_os
  VERSION 0.1.0
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
option(RPGOS_BUILD_TESTS        "Build the test suite"             ON)
option(RPGOS_ENABLE_SANITIZERS  "Build with ASan + UBSan"          OFF)
option(RPGOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# --------------------------------------------------------------------------
# Library target.
# The public API lives in include/rpgos/, implementation in src/.
#
# Placeholder: an INTERFACE target keeps the build valid while the library is
# empty (no code yet). As soon as the first source file exists, replace it with
#   add_library(rpgos STATIC src/....cpp)
#   add_library(rpgos::rpgos ALIAS rpgos)
# and the warning/sanitizer setup below is applied automatically.
# --------------------------------------------------------------------------
add_library(rpgos INTERFACE)
add_library(rpgos::rpgos ALIAS rpgos)

target_compile_features(rpgos INTERFACE cxx_std_23)
target_include_directories(rpgos INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

get_target_property(_rpgos_type rpgos TYPE)
if(NOT _rpgos_type STREQUAL "INTERFACE_LIBRARY")
  set_project_warnings(rpgos)
  enable_sanitizers(rpgos)
endif()
unset(_rpgos_type)

# Packaging / install rules go here once the API is stable, e.g.:
#   include(GNUInstallDirs)
#   install(TARGETS rpgos EXPORT rpgosTargets
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
#   install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(RPGOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
