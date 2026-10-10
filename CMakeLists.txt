cmake_minimum_required(VERSION 3.24)

# Dependency selection must precede project(): the vcpkg toolchain installs the
# manifest while project() initializes the compiler

# rocky-lite: a fully offline configuration built only from CMake + a C++
# compiler + the Vulkan SDK, with no vcpkg/Conan and a drastically reduced
# dependency set (see ROCKY_LITE_SPEC.md and docs/ROCKY_LITE_NOTES.md).
# ON by default; set to OFF to get the original vcpkg-oriented defaults
# below (GDAL, MBTiles, HTTPS, Slughorn, Azure all enabled). ImGui stays
# on in both configurations -- see the note below.
option(ROCKY_LITE "Build the dependency-pruned, fully offline configuration" ON)

# optional modules
# NOTE: when ROCKY_LITE=ON, the defaults below are overridden further down
# (after project()) rather than here, so each option's documented default
# stays accurate for the ROCKY_LITE=OFF / upstream-compatible configuration.
option(ROCKY_SUPPORTS_AZURE "Support Azure Maps (subscription required)" ON)
option(ROCKY_SUPPORTS_BING "Support Bing Maps (subscription required)" OFF)
option(ROCKY_SUPPORTS_CURL "Support HTTP with the CURL library" ON)
option(ROCKY_SUPPORTS_GDAL "Support GeoTIFF, WMS, WMTS, and other GDAL formats (requires gdal)" ON)
option(ROCKY_SUPPORTS_HTTPLIB "Support HTTP with the lightweight cpp-httplib library" OFF)
option(ROCKY_SUPPORTS_HTTPS "Support HTTPS (requires openssl)" ON)
option(ROCKY_SUPPORTS_IMGUI "Support Dear ImGui and build ImGui-based demos" ON)
option(ROCKY_SUPPORTS_MBTILES "Support MBTiles databases with extended spatial profile support (requires sqlite3, zlib)" ON)
option(ROCKY_SUPPORTS_QT "Build Qt demos" OFF)
option(ROCKY_SUPPORTS_SLUGHORN "Support Slughorn vector decals" ON)

if(ROCKY_LITE)
    # Force the lite-mode combination of the options above. FORCE is used
    # deliberately: ROCKY_LITE=ON is meant to mean "fully lite" even if the
    # cache already has the upstream defaults from a prior non-lite
    # configure. To get GDAL/MBTiles/HTTPS/Slughorn/Azure back, configure
    # with -DROCKY_LITE=OFF instead of fighting these.
    #
    # ImGui is deliberately left ON (not forced off): Label/Widget
    # rendering (src/rocky/vsg/ecs/LabelSystem.cpp, WidgetSystem.cpp) is
    # implemented entirely on top of ImGui with no non-ImGui path, so
    # treating it as prunable the way GDAL/sqlite/curl are would silently
    # remove a load-bearing rendering feature rather than an add-on. It's
    # also small, MIT-licensed, and vendored the same way as everything
    # else (third_party/imgui, see third_party/CMakeLists.txt), so keeping
    # it costs nothing toward the offline/no-vcpkg goal.
    set(ROCKY_SUPPORTS_AZURE OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_CURL OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_GDAL OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_HTTPLIB ON CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_HTTPS OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_MBTILES OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_QT OFF CACHE BOOL "" FORCE)
    set(ROCKY_SUPPORTS_SLUGHORN OFF CACHE BOOL "" FORCE)

    # Vendored third-party targets (glm, spdlog, glslang, SPIRV, ...) are
    # built from source as ordinary in-tree targets, not as IMPORTED
    # targets from a find_package()-located install -- so Rocky's and
    # VSG's own install(EXPORT ...) calls would otherwise fail at generate
    # time demanding that every such dependency also be part of an export
    # set. rocky-lite has no goal of producing a redistributable installed
    # SDK (only of building and running the example), so skip install()
    # processing entirely rather than solve that per dependency.
    set(CMAKE_SKIP_INSTALL_RULES ON)
endif()

# whether to copy dependency DLLs (under windows, usually) to the installation destination
option(ROCKY_INSTALL_VCPKG_DEPENDENCY_DLLS "Copy dependency DLLs from vcpkg to the install destination" OFF)
option(ROCKY_RENDERER_VSG "Build the Vulkan/VSG rendering engine" ON)
option(ROCKY_USE_UTF8_FILENAMES_ON_WINDOWS "" ON)

mark_as_advanced(ROCKY_RENDERER_VSG)
mark_as_advanced(ROCKY_SUPPORTS_HTTPLIB)
mark_as_advanced(ROCKY_SUPPORTS_CURL)
mark_as_advanced(ROCKY_USE_UTF8_FILENAMES_ON_WINDOWS)

set(SLUGHORN_SOURCE_DIR "" CACHE PATH "Use an existing Slughorn source checkout")

# if using vcpkg, configure overlay ports (that are not available in vcpkg prime)
if ("${CMAKE_TOOLCHAIN_FILE}" MATCHES ".*vcpkg.cmake.*")
    message(STATUS "Building with vcpkg toolchain.")
    set(USING_VCPKG ON)
    set(VCPKG_OVERLAY_PORTS ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-ports)
endif()

# Main PROJECT definition
project(
    rocky
    VERSION 1.1.0
    DESCRIPTION "Rocky by Pelican Mapping"
    HOMEPAGE_URL https://github.com/pelicanmapping/rocky
    LANGUAGES CXX C
)

# please update this with each ABI change!!
set(PROJECT_VERSION_ABI 58)

# require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# define that we can use when checking for conditional features in the SDK build
add_definitions(-DROCKY_BUILDING_SDK)

# option to build shared or static libs
option(ROCKY_BUILD_SHARED_LIBS "Build shared libraries" ON)
mark_as_advanced(ROCKY_BUILD_SHARED_LIBS)

# whether to append the ABI version to shared library filenames
option(ROCKY_APPEND_SO_VERSION "Whether to append the ABI version to libraries" ON)
mark_as_advanced(ROCKY_APPEND_SO_VERSION)

# folder for additional cmake includes
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# temporary folder to hold build-time header compilation (.in files built with compile_files)
set(ROCKY_BUILDTIME_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/build_include")
include_directories(${ROCKY_BUILDTIME_INCLUDE_DIR})

# temporary folder to hold shared resources (like shaders) for use when running from the IDE
set(ROCKY_BUILDTIME_SHARE_DIR "${CMAKE_CURRENT_BINARY_DIR}/build_share")

# set up standard install folder names, CMAKE_INSTALL_*
# https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html
include(GNUInstallDirs)

# load utilities
include("cmake/install-export-files.cmake")

set(BUILD_WITH_JSON ON)

if(ROCKY_SUPPORTS_GDAL)
    set(BUILD_WITH_GDAL ON)
endif()

if(ROCKY_SUPPORTS_HTTPLIB)
    set(BUILD_WITH_HTTPLIB ON)
endif()

if(ROCKY_SUPPORTS_HTTPS)
    set(BUILD_WITH_OPENSSL ON)
endif()

if (ROCKY_SUPPORTS_MBTILES)
    set(BUILD_WITH_SQLITE3 ON)
    set(BUILD_WITH_ZLIB ON)
endif()

if(ROCKY_SUPPORTS_QT)
    set(BUILD_WITH_QT ON)
endif()

if(ROCKY_SUPPORTS_CURL)
    if(BUILD_WITH_HTTPLIB)
        message(WARNING "You selected both HTTPLIB and CURL. These are mutually exclusive, so I am disabling CURL.")
    else()
        set(BUILD_WITH_CURL ON)
    endif()
endif()

if(ROCKY_SUPPORTS_IMGUI)
    set(BUILD_WITH_IMGUI ON)
endif()

# file(GLOB) what excludes anything starting with a '.'
function(rocky_glob out_var)
    file(GLOB _items ${ARGN})
    list(FILTER _items EXCLUDE REGEX "(^|/)\\.[^/]*$")
    set(${out_var} ${_items} PARENT_SCOPE)
endfunction()

# rocky-lite: vendored third-party dependencies, added before src/ so that
# their FetchContent(SOURCE_DIR ... OVERRIDE_FIND_PACKAGE) redirects are in
# place before any find_package() call below (or inside VSG/vsgXchange's
# own CMakeLists.txt files) resolves them.
if(ROCKY_LITE)
    add_subdirectory(third_party)
endif()

# source code
add_subdirectory(src)

# Strict conformance
add_compile_options(
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->
)
