# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

# Example programs.
#
# Small self-contained demos that show both engine modes on the shipped
# rulesets. They are not part of the library and are not installed.

add_executable(rpg_os_example_universal universal_mode/main.cpp)
target_link_libraries(rpg_os_example_universal PRIVATE rpg_os::rpg_os)
set_project_warnings(rpg_os_example_universal)
enable_sanitizers(rpg_os_example_universal)

add_executable(rpg_os_example_specific specific_mode/main.cpp)
target_link_libraries(rpg_os_example_specific PRIVATE rpg_os::rpg_os)
set_project_warnings(rpg_os_example_specific)
enable_sanitizers(rpg_os_example_specific)

add_executable(rpg_os_example_fight fight/main.cpp)
target_link_libraries(rpg_os_example_fight PRIVATE rpg_os::rpg_os)
set_project_warnings(rpg_os_example_fight)
enable_sanitizers(rpg_os_example_fight)

# Entity-component-system integration examples. The first shows the pattern
# with plain id-keyed maps (no third-party ECS); the second uses EnTT
# (vendored single header under include/rpg_os/third_party/entt/) as the
# storage layer. Both wrap the universal DynamicEntity as a component and use
# the observer events to keep presentation state in sync.
add_executable(rpg_os_example_ecs_registry ecs_registry/main.cpp)
target_link_libraries(rpg_os_example_ecs_registry PRIVATE rpg_os::rpg_os)
set_project_warnings(rpg_os_example_ecs_registry)
enable_sanitizers(rpg_os_example_ecs_registry)

add_executable(rpg_os_example_ecs_entt ecs_entt/main.cpp)
target_link_libraries(rpg_os_example_ecs_entt PRIVATE rpg_os::rpg_os)
set_project_warnings(rpg_os_example_ecs_entt)
enable_sanitizers(rpg_os_example_ecs_entt)
