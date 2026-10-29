find_package(Threads REQUIRED)

# Reuse the same verifier sources as the authoritative shell runners. Link the
# configured core once so checks exercise its selected compiler/build flags.
file(GLOB verifier_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
foreach(verifier_source IN LISTS verifier_sources)
    get_filename_component(verifier_name "${verifier_source}" NAME_WE)
    add_executable(${verifier_name} "${verifier_source}")
    target_link_libraries(${verifier_name} PRIVATE gba_core Threads::Threads)
    if(MSVC)
        target_compile_options(${verifier_name} PRIVATE /W4)
    else()
        target_compile_options(${verifier_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${verifier_name} COMMAND ${verifier_name})
    set_tests_properties(${verifier_name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()
