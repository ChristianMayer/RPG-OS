# AddressSanitizer + UndefinedBehaviourSanitizer.
#
# Usage (in CMakeLists.txt):
#   enable_sanitizers(<target>)
#
# No-op unless the project option RPGOS_ENABLE_SANITIZERS is ON:
#   cmake -B build -DRPGOS_ENABLE_SANITIZERS=ON

function(enable_sanitizers target)
  if(NOT RPGOS_ENABLE_SANITIZERS)
    return()
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "enable_sanitizers: unknown target '${target}'")
  endif()
  if(MSVC)
    message(WARNING "Sanitizers are not wired up for MSVC.")
    return()
  endif()
  target_compile_options(${target} PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE
    -fsanitize=address,undefined)
endfunction()
