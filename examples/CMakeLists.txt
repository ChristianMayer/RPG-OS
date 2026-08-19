# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

# Example programs.
#
# Small self-contained demos that show both engine modes on the shipped
# rulesets. They are not part of the library and are not installed.

# Registers one example executable with the project warnings + sanitizers.
function(add_rpg_os_example name source)
  add_executable("${name}" "${source}")
  target_link_libraries("${name}" PRIVATE rpg_os::rpg_os)
  set_project_warnings("${name}")
  enable_sanitizers("${name}")
endfunction()

add_rpg_os_example(rpg_os_example_universal universal_mode/main.cpp)
add_rpg_os_example(rpg_os_example_specific specific_mode/main.cpp)
add_rpg_os_example(rpg_os_example_fight fight/main.cpp)

# Entity-component-system integration examples. The first shows the pattern
# with plain id-keyed maps (no third-party ECS); the second uses EnTT
# (vendored single header under include/rpg_os/third_party/entt/) as the
# storage layer. Both wrap the universal DynamicEntity as a component and use
# the observer events to keep presentation state in sync.
add_rpg_os_example(rpg_os_example_ecs_registry ecs_registry/main.cpp)
add_rpg_os_example(rpg_os_example_ecs_entt ecs_entt/main.cpp)
