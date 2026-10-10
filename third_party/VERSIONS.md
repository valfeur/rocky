# Vendored third-party dependencies

Everything in `third_party/` is a trimmed, pinned copy of the upstream
source tree below (no submodules, no `FetchContent` downloads -- see
`third_party/CMakeLists.txt`). Trimming removed each project's own tests,
docs, benchmarks and CLI tools (none of which rocky-lite builds or needs);
nothing in the *library* source each dependency builds was altered except
where noted.

Re-vendoring (bumping a pin): shallow-clone the tag/commit below, copy the
listed paths into `third_party/<name>/`, drop the `.git` directory, and
re-run the trim. No script does this automatically yet -- do it by hand
and update the commit hash here.

| Dependency | Upstream | Pinned tag | Commit | License | Vendored paths |
|---|---|---|---|---|---|
| glm | https://github.com/g-truc/glm | `1.0.3` | `8d1fd52e5ab5590e2c81768ace50c72bae28f2ed` | MIT / MIT-like (see `LICENSE.txt`) | `CMakeLists.txt`, `cmake/`, `glm/`, `LICENSE.txt` |
| EnTT | https://github.com/skypjack/entt | `v3.13.2` | `78213075654a688e9da6bc49f7f873d25c26d12c` | MIT | `CMakeLists.txt`, `cmake/`, `src/`, `LICENSE` |
| spdlog | https://github.com/gabime/spdlog | `v1.17.0` | (see repo tag) | MIT (bundles `fmt`, also MIT, under `include/spdlog/fmt/bundled/`) | `CMakeLists.txt`, `cmake/`, `include/`, `src/`, `LICENSE` |
| nlohmann/json | https://github.com/nlohmann/json | `v3.12.0` | (see repo tag) | MIT | `CMakeLists.txt`, `cmake/`, `include/`, `nlohmann_json.natvis`, `LICENSES/` |
| cpp-httplib | https://github.com/yhirose/cpp-httplib | `v0.60.1` | (see repo tag) | MIT | `httplib.h`, `LICENSE` (vendored as a bare header; no TLS/zlib/brotli support macros defined) |
| Dear ImGui | https://github.com/ocornut/imgui | `v1.92.5` | `6d910d5487d11ca567b61c7824b0c78c569d62f0` | MIT | `imgui.{h,cpp}`, `imgui_demo/draw/tables/widgets.cpp`, `imconfig.h`, `imgui_internal.h`, `imstb_*.h`, `backends/imgui_impl_vulkan.{h,cpp}`, `LICENSE.txt` -- no GLFW/SDL/Win32/DX backend, matching the vulkan-only feature the existing `vcpkg-ports/imgui` overlay port pinned |
| stb (`stb_image.h` only) | https://github.com/nothings/stb | n/a (no tags) | `013ac3beddff3dbffafd5177e7972067cd2b5083` | Public domain / MIT (dual-licensed, see header comment) | `stb_image.h` |
| cppmap3d | https://github.com/ClancyWalters/cppmap3d | `1.3.0` | `1394b6ec588242584c1d4f21308d36cdfa0e5926` | Apache-2.0 | `cppmap3d.hh`, `LICENSE` |
| VulkanSceneGraph (VSG) | https://github.com/vsg-dev/VulkanSceneGraph | `v1.1.16` | `77839c704362d832c027cc878e5496a14972e83d` | MIT | Full tree minus `.git`, `docs/`, `.github/` |
| vsgXchange | https://github.com/vsg-dev/vsgXchange | `v1.1.14` | `73d6345769aaf01502cd13b083ce48a10b057ed2` | MIT | Full tree minus `.git`, `.github/` |
| glslang | https://github.com/KhronosGroup/glslang | `vulkan-sdk-1.4.321.0` | `8a85691a0740d390761a1008b4696f57facd02c4` | BSD-3-Clause / Apache-2.0 (dual, see `LICENSE.txt`) | `CMakeLists.txt`, `parse_version.cmake`, `build_info.h.tmpl`, `CHANGES.md`, `BUILD.gn`, `glslang/`, `SPIRV/`, `build_overrides/`, `External/` (dropped: `Test/`, `gtests/`, `StandAlone/`, `kokoro/`, `ndk_test/` -- none needed to build the library) |

## Not vendored (replaced by upstream's own graceful degradation)

vsgXchange's optional backends (`assimp`, `Freetype`, `draco`, `CURL`,
`OpenEXR`, `GDAL`, `ktx`) each self-detect via their own
`find_package(... QUIET)` in `src/<backend>/build_vars.cmake` and compile a
stub/fallback source file when not found -- this is upstream's own
existing behavior, not a lite-specific patch. None of those seven
libraries are present in a lite build, so vsgXchange builds with only its
built-in `stbi`/`dds`/plain-`gltf` readers enabled. (vsgXchange also
vendors its own internal copy of `stb_image.h` under `src/stbi/` --
independent of, and possibly redundant with, the `third_party/stb` copy
above; whether Rocky's own code ends up needing the latter directly is a
Phase 3 decision, see `docs/ROCKY_LITE_NOTES.md`.)

## Why `glslang` is here even though `glslangValidator` isn't required

Phase 0 found that Rocky never invokes the `glslangValidator` CLI tool at
build time -- GLSL shaders are compiled to SPIR-V at *runtime* by VSG's
own `vsg::ShaderCompileSettings`/shader-reading machinery, which links
against the `glslang` *library*. So glslang is vendored here purely as a
link dependency of VSG (`VSG_SUPPORTS_ShaderCompiler`), trimmed of the
`StandAlone/` directory that builds the `glslangValidator` and
`spirv-remap` executables neither VSG nor Rocky need. `ENABLE_OPT` is
forced `OFF` (see `third_party/CMakeLists.txt`) so glslang never looks for
a SPIRV-Tools-opt install either -- VSG degrades
`VSG_SUPPORTS_ShaderOptimizer` to `OFF` cleanly when that's absent.

## Build-tree wiring

`third_party/CMakeLists.txt` declares each dependency with
`FetchContent_Declare(<name> SOURCE_DIR <vendored path> OVERRIDE_FIND_PACKAGE)`.
Because `SOURCE_DIR` already points at the vendored copy, FetchContent
never downloads anything; `OVERRIDE_FIND_PACKAGE` makes every existing
`find_package(<name> ...)` call elsewhere in the tree (in
`src/rocky/CMakeLists.txt`, and inside VSG's/vsgXchange's own
`CMakeLists.txt` files, unmodified) transparently resolve to the vendored
copy instead of searching vcpkg or the system. `cpp-httplib` is the one
exception -- Rocky locates it via `find_path(CPP_HTTPLIB_INCLUDE_DIRS
"httplib.h")` rather than `find_package()`, so its cache variable is
pre-seeded directly instead.

`CMAKE_SKIP_INSTALL_RULES` is forced `ON` in `ROCKY_LITE` builds (see root
`CMakeLists.txt`): rocky-lite's goal is building and running the example,
not producing a redistributable installed SDK, and several vendored
dependencies (`glm`, `spdlog` as in-tree subproject builds) don't export
themselves for `install(EXPORT ...)` re-export the way an installed
vcpkg/CONFIG package would. One exception remains explicit in
`third_party/CMakeLists.txt`: glslang's targets (`glslang`, `SPIRV`,
`glslang-default-resource-limits`) are registered into VSG's own
`vsgTargets` export set via `install(TARGETS ... EXPORT vsgTargets)`,
because VSG's build additionally performs a build-tree
`export(EXPORT vsgTargets ...)` (not an `install()` command, so unaffected
by `CMAKE_SKIP_INSTALL_RULES`) that validates export-set completeness
regardless.

## Deliberately not pinned to the newest release

- **EnTT**: latest upstream tag is `v4.0.0`, but Rocky's ECS usage
  (`entt::registry`, `entt::entity`, basic view/observer patterns) is
  exercised against the long-stable v3.x API. Pinned to `v3.13.2` (last
  mature pre-4.0 release) rather than the newest tag, to minimize the risk
  of an unreviewed breaking-API jump; nothing stops bumping this later.
- **glslang**: pinned to a recent-but-not-bleeding-edge Vulkan SDK tag
  rather than the literal latest, since any reasonably current tag
  satisfies VSG's needs and this reduces exposure to a just-published
  release.

## PROJ is intentionally absent from this list

PROJ is not vendored (see `docs/ROCKY_LITE_NOTES.md` for the full
analysis): it hard-requires SQLite3 both to build (`proj.db` is generated
from `.sql` sources via the `sqlite3` executable) and at runtime (to query
`proj.db`, a multi-megabyte binary CRS database) -- which would reintroduce
exactly the "no sqlite, no data files" constraints section 3 of
`ROCKY_LITE_SPEC.md` asks to remove. `SRS.cpp`'s PROJ usage is being
replaced with closed-form math instead (Phase 2), optionally assisted by
`cppmap3d` (vendored above) for the geodetic/ECEF/ENU conversions.
