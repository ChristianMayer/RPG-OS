# Test suite for rpgos.
#
# The test framework has not been chosen yet (Catch2 / doctest / GoogleTest
# are all fine). Once test files exist, register them with CTest, e.g.:
#
#   find_package(Catch2 3 REQUIRED)
#   add_executable(rpgos_tests test_main.cpp)
#   target_link_libraries(rpgos_tests PRIVATE rpgos::rpgos Catch2::Catch2WithMain)
#   include(Catch)
#   catch_discover_tests(rpgos_tests)
#
# or the simpler:
#
#   add_executable(rpgos_tests test_main.cpp)
#   target_link_libraries(rpgos_tests PRIVATE rpgos::rpgos)
#   add_test(NAME rpgos_tests COMMAND rpgos_tests)
