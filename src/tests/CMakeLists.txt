set(APP_NAME rocky_tests)

set(SOURCES tests.cpp tests_projection_lifecycle.cpp tests_tessellation.cpp tests_slug_fallback.cpp
    tests_terrain_anchor.cpp tests_overlay_depth.cpp tests_overlay_lifecycle.cpp tests_texture.cpp tests_units.cpp
    tests_lite_tiles.cpp catch.hpp)

add_executable(${APP_NAME} ${SOURCES})

target_link_libraries(${APP_NAME} rocky)

# tests_lite_tiles.cpp validates rocky-lite's Phase 3 tile-loading story
# (local-directory tiles, a real plain-http local server). stb_image_write.h
# is vendored at third_party/stb regardless of ROCKY_LITE (it's just a
# header on disk, not wired through third_party/CMakeLists.txt's
# FetchContent targets), and cpp-httplib's header likewise -- include both
# directly rather than depending on the ROCKY_LITE-only interface targets,
# so this still builds in a non-lite (ROCKY_LITE=OFF) configuration.
target_include_directories(${APP_NAME} PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/stb"
    "${PROJECT_SOURCE_DIR}/third_party/cpp-httplib")

if(WIN32)
    target_link_libraries(${APP_NAME} ws2_32)
endif()

# Catch and the ECS templates can exceed the COFF section limit in MSVC debug-info builds.
target_compile_options(${APP_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

# Tests use json.h, which relies on nlohmann_json, which is not a public dependency of rocky
if (BUILD_WITH_JSON)
    find_package(nlohmann_json CONFIG)
    if (nlohmann_json_FOUND)
        target_link_libraries(${APP_NAME} nlohmann_json::nlohmann_json)
    endif()
endif()


install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)

set_target_properties(${APP_NAME} PROPERTIES FOLDER "tests")
