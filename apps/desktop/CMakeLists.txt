set(GBA_DESKTOP_SOURCES
    main.cpp
)

add_executable(gba-desktop ${GBA_DESKTOP_SOURCES})

target_link_libraries(gba-desktop PRIVATE gba_core SDL3::SDL3)
target_include_directories(gba-desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Engine identity for lab artifacts. FALLBACK keeps tarball-without-git builds
# reproducible rather than silently claiming "unknown".
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    OUTPUT_VARIABLE GBA_DESKTOP_ENGINE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GBA_DESKTOP_GIT_RESULT
)
if(NOT GBA_DESKTOP_GIT_RESULT EQUAL 0 OR GBA_DESKTOP_ENGINE_COMMIT STREQUAL "")
    set(GBA_DESKTOP_ENGINE_COMMIT "unknown")
endif()

target_compile_definitions(gba-desktop PRIVATE
    GBA_ENGINE_COMMIT="${GBA_DESKTOP_ENGINE_COMMIT}"
    GBA_BUILD_TYPE="$<CONFIG>"
)
