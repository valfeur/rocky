# rocky-lite: vendored third-party dependencies.
#
# Every dependency below is a trimmed, pinned copy of its upstream source
# tree, vendored directly in this directory (see VERSIONS.md for exact
# commits, upstream URLs and licenses). Nothing here is downloaded by
# CMake: FetchContent is used only in its "already have the source"
# mode (SOURCE_DIR pointing at the vendored copy, OVERRIDE_FIND_PACKAGE),
# which makes every existing find_package(<name> ...) call elsewhere in
# the tree (src/rocky/CMakeLists.txt, and upstream VSG/vsgXchange's own
# CMakeLists.txt files) resolve to the vendored copy instead of searching
# the system or vcpkg -- with no changes needed to those find_package
# call sites.
#
# This file is only added (see root CMakeLists.txt) when ROCKY_LITE=ON.

include(FetchContent)

# Belt-and-suspenders: guarantee no network fetch is ever attempted here,
# even if a future edit adds a FetchContent_Declare() without SOURCE_DIR.
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)

set(ROCKY_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- glm (header-only math) ----
FetchContent_Declare(glm
    SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/glm"
    OVERRIDE_FIND_PACKAGE)

# ---- EnTT (header-only ECS) ----
set(ENTT_INCLUDE_HEADERS OFF CACHE BOOL "" FORCE)
set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(EnTT
    SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/entt"
    OVERRIDE_FIND_PACKAGE)

# ---- spdlog (logging; builds with its own bundled fmt, no extra dep) ----
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/spdlog"
    OVERRIDE_FIND_PACKAGE)

# ---- nlohmann_json (header-only JSON) ----
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install ON CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/json"
    OVERRIDE_FIND_PACKAGE)

# ---- cpp-httplib (header-only HTTP; no TLS/zlib/brotli support compiled in) ----
# Rocky finds this via find_path(CPP_HTTPLIB_INCLUDE_DIRS "httplib.h"), not
# find_package, so there is no FetchContent redirect for it -- pre-seed the
# cache variable instead; find_path() is a no-op once its result variable
# already has a value.
set(CPP_HTTPLIB_INCLUDE_DIRS "${ROCKY_THIRD_PARTY_DIR}/cpp-httplib"
    CACHE PATH "Vendored cpp-httplib include directory" FORCE)

# ---- Dear ImGui (vulkan-binding only; no GLFW/SDL/Win32 backend -- Rocky
#      feeds VSG's own input events into ImGui itself, see
#      src/rocky/vsg/imgui/SendEventsToImGui.h). Rocky's own
#      cmake/FindImGui.cmake already has a from-source build path keyed
#      off IMGUI_DIR -- no custom CMakeLists.txt needed here, unlike the
#      other vendored dependencies above.
if(ROCKY_SUPPORTS_IMGUI)
    set(IMGUI_DIR "${ROCKY_THIRD_PARTY_DIR}/imgui" CACHE PATH "Vendored Dear ImGui source directory" FORCE)
endif()

# ---- stb_image (single-header image decode; wired up in Phase 3) ----
if(NOT TARGET stb::stb_image)
    add_library(rocky_stb_image INTERFACE)
    add_library(stb::stb_image ALIAS rocky_stb_image)
    target_include_directories(rocky_stb_image INTERFACE "${ROCKY_THIRD_PARTY_DIR}/stb")
endif()

# ---- cppmap3d (header-only geodetic/ECEF/ENU math; wired up in Phase 2) ----
if(NOT TARGET cppmap3d::cppmap3d)
    add_library(rocky_cppmap3d INTERFACE)
    add_library(cppmap3d::cppmap3d ALIAS rocky_cppmap3d)
    target_include_directories(rocky_cppmap3d INTERFACE "${ROCKY_THIRD_PARTY_DIR}/cppmap3d")
endif()

if(ROCKY_RENDERER_VSG)
    # ---- glslang (GLSL -> SPIR-V compiler library; linked into VSG at
    #      build time for its runtime ShaderCompiler -- never invoked as
    #      the glslangValidator CLI tool). Binaries/tests/optimizer trimmed
    #      out of the vendored copy and disabled here, so no SPIRV-Tools
    #      dependency is pulled in.
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
        SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/glslang"
        OVERRIDE_FIND_PACKAGE)

    # Populate glslang now (rather than waiting for VSG's own
    # find_package(glslang) to trigger it lazily) so its targets exist in
    # this scope. VSG's own CMake helper macro does a build-tree
    # export(EXPORT vsgTargets ...) of itself that requires glslang/SPIRV
    # to be part of SOME install(... EXPORT ...) set -- GLSLANG_ENABLE_INSTALL
    # is OFF above (we don't need glslang installable on its own), so add
    # them to vsg's own "vsgTargets" export set ourselves. This registers
    # export-set membership regardless of CMAKE_SKIP_INSTALL_RULES, which
    # only suppresses the later install *action*, not this bookkeeping.
    find_package(glslang CONFIG REQUIRED)
    install(TARGETS glslang SPIRV glslang-default-resource-limits
        EXPORT vsgTargets)

    # ---- VSG (Vulkan Scene Graph -- the rendering engine) ----
    FetchContent_Declare(vsg
        SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/VulkanSceneGraph"
        OVERRIDE_FIND_PACKAGE)

    # ---- vsgXchange (file format readers/writers for VSG). Each optional
    #      backend (assimp/freetype/draco/CURL/OpenEXR/GDAL/ktx -- the
    #      features the vcpkg overlay port pinned) self-detects via its own
    #      find_package(... QUIET) in src/<backend>/build_vars.cmake and
    #      falls back to a stub cleanly when not found. None of those
    #      libraries are vendored or available in the lite tree, so they
    #      disable themselves automatically; only the built-in stb-based
    #      image reader (src/stbi, vendored in-tree by vsgXchange itself)
    #      and the dependency-free readers (dds, gltf without draco, etc.)
    #      end up enabled.
    FetchContent_Declare(vsgXchange
        SOURCE_DIR "${ROCKY_THIRD_PARTY_DIR}/vsgXchange"
        OVERRIDE_FIND_PACKAGE)
endif()
