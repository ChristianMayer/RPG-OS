# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

# Compiler warning setup.
#
# Usage (in CMakeLists.txt):
#   set_project_warnings(<target>)
#
# Honors the project option RPG_OS_WARNINGS_AS_ERRORS.

function(set_project_warnings target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "set_project_warnings: unknown target '${target}'")
  endif()

  if(RPG_OS_WARNINGS_AS_ERRORS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /WX)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wdouble-promotion
      -Wformat=2
      -Wnull-dereference
      -Wmisleading-indentation
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(${target} PRIVATE
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op)
    endif()
  endif()
endfunction()
