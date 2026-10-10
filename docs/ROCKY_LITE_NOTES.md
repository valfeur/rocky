# rocky-lite notes — Phase 0 audit

Status: audit only, no functional changes made. This resolves every
`[VERIFY]` tag in `ROCKY_LITE_SPEC.md` against the actual code as of
commit `d93ffb8` (branch `main`).

## 1. Full dependency list (`vcpkg.json`)

| Dependency | Purpose | Consumption | Lite build |
|---|---|---|---|
| `cpp-httplib` | HTTP client/server, header-only | header-only, opt-in via `ROCKY_SUPPORTS_HTTPLIB` (default **OFF** today) | **keep**, make default ON |
| `entt` | ECS | header-only | **keep** |
| `gdal` (+webp) | Raster/vector GIS formats, `Geocoder` | linked, gated by `ROCKY_SUPPORTS_GDAL` (default ON) | **remove** (flip default OFF) |
| `glm` | vector/matrix math | header-only | **keep** |
| `imgui` (+vulkan-binding, custom overlay port) | debug UI / demos | linked, gated by `ROCKY_SUPPORTS_IMGUI` (default ON) | **keep optional**, default OFF in lite |
| `nlohmann-json` | JSON (de)serialization of Layer configs | header-only | **keep** |
| `openssl` | TLS for cpp-httplib/curl | linked, gated by `ROCKY_SUPPORTS_HTTPS` (default ON) | **remove** |
| `proj` (+tools) | `SRS` class — all projection math | linked, **not optional today** (`find_package(PROJ CONFIG REQUIRED)` with no option gate) | **remove**, replace per spec §3 |
| `spdlog` | logging | linked (header-mostly) | **keep** |
| `slughorn` (custom overlay port, `AlphaPixel/slughorn`) | vector-decal text rendering, `ROCKY_SUPPORTS_SLUGHORN` (default ON) | linked; **falls back to a `FetchContent` download from GitHub if not found as an installed package** | **remove** (flip default OFF — a build-time network fetch exists here today, see §4.3) |
| `sqlite3` | MBTiles format only | linked, gated by `ROCKY_SUPPORTS_MBTILES` (default ON) | **remove** (flip default OFF) |
| `vsg` (+windowing) | Vulkan Scene Graph — core renderer | linked, required, `find_package(vsg CONFIG REQUIRED)` | **keep**, vendor instead of vcpkg |
| `vsgxchange` (+assimp, freetype, draco, custom overlay port) | file format readers/writers (images, models, fonts) for VSG | linked, **optional** (`find_package(vsgXchange CONFIG)`, not `REQUIRED`); `ROCKY_HAS_VSGXCHANGE` guards all use sites | **keep, but vendor a trimmed build** (drop assimp/freetype/draco features — see §4.7) |
| `zlib` | MBTiles decompression only | linked, gated with sqlite | **remove** (flip default OFF) |

`CMakeLists.txt` root options already existing and directly useful for lite mode:
`ROCKY_SUPPORTS_GDAL`, `ROCKY_SUPPORTS_CURL`, `ROCKY_SUPPORTS_HTTPLIB`,
`ROCKY_SUPPORTS_HTTPS`, `ROCKY_SUPPORTS_IMGUI`, `ROCKY_SUPPORTS_MBTILES`,
`ROCKY_SUPPORTS_SLUGHORN`. **There is no equivalent option for PROJ** —
it is the one hard-required dependency with no existing off-switch.

## 2. Reference-site counts (effort estimate)

Grep over `src/` (`.h`/`.cpp`/`.in`), occurrence counts, not just file counts:

- **GDAL**: 237 occurrences across 24 files. Heavily concentrated in
  dedicated, already-optional files: `GDAL.{h,cpp}` (75), `GDALImageLayer.*`
  (31), `GDALElevationLayer.*` (32), `GDALFeatureSource.*` (38),
  `rocky_demo` feature-layer demo headers (16, all trivially excludable —
  they're demo pages, not core). Outside those: `Context.cpp` (14, mostly
  one `ROCKY_HAS_GDAL`-guarded registration block), `VSGContext.cpp` (9, the
  `GDAL_VSG_ReaderWriter` registration already shown ifdef'd above),
  `Geocoder.cpp` (2), `Feature.cpp` (1), `GeoImage.cpp` (1), plus
  `Version.h.in`/`rocky.h`/`rocky-config.cmake.in` (capability macros).
  **Coupling is shallow**: nothing in the terrain engine or core tile
  pipeline requires GDAL to be present to compile or run with it disabled
  (`ROCKY_SUPPORTS_GDAL=OFF` is already a supported, CI-adjacent
  configuration pattern — see §4.3).
- **PROJ**: 107 occurrences, but 100 of them are in one file, `SRS.cpp`,
  plus 7 incidental ones in `tests.cpp`. **This is the one genuinely
  concentrated dependency** — see §3 below. No other file in the engine
  calls into PROJ directly.
- **sqlite / curl / OpenSSL / zlib**: small, isolated, already each behind
  their own CMake option (`MBTiles.{h,cpp}` for sqlite; `URI.cpp` for
  curl/OpenSSL/zlib — zlib is actually *only* referenced for MBTiles output
  compression despite being globally "available," see `URI.cpp` ifdefs).

**Combined total is 344 occurrences, which nominally exceeds the spec's
~150-site stop threshold — but the threshold's intent (checking for deep
coupling of the terrain engine to GDAL/PROJ) does not hold here.** GDAL
touches only format-specific leaf classes already gated by an existing
build option; PROJ touches exactly one class (`SRS`) with a well-defined
public surface. See §8 (recommendation) — proceeding to Phase 1/2 looks
safe without the "alternative: build directly on VSG" escape hatch the
spec describes for the deep-coupling case.

## 3. SRS / PROJ coupling detail

`[VERIFY]` confirmed: `src/rocky/SRS.cpp` wraps PROJ directly and
exclusively (`#include <proj.h>`, `PJ*`/`PJ_CONTEXT*` handles, a
per-thread `SRSFactory` cache keyed by proj string/WKT/EPSG code/alias).
Unlike every other optional dependency, **PROJ has no CMake option** — it
is `find_package(PROJ CONFIG QUIET)` with a `pkg-config` fallback, and
`ROCKY_HAS_PROJ` is unconditionally set `TRUE`. `SRS.h`'s public API
(`SRS::WGS84`, `::ECEF`, `::SPHERICAL_MERCATOR`, string-based construction,
`SRSOperation` for transforms) is the only thing other code depends on —
callers never touch PROJ types directly. This matches the spec's plan:
implement the four required systems in closed form behind `SRS`'s existing
shape, put the current PROJ implementation behind a new `ROCKY_HAS_PROJ`
*option* (today it's a non-optional `#define`), default it OFF.

## 4. How VSG / vsgXchange / ImGui are obtained

All three are **vcpkg-only today**, via plain `find_package(... CONFIG
REQUIRED/QUIET)` in `src/rocky/CMakeLists.txt`:
- `find_package(vsg CONFIG REQUIRED)` — required, no fallback.
- `find_package(vsgXchange CONFIG)` — **not** `REQUIRED`; if absent,
  `ROCKY_HAS_VSGXCHANGE` is simply undefined and all its use sites
  (`VSGContext.cpp`) compile it out. Comes from vcpkg with a custom overlay
  port at `vcpkg-ports/vsgxchange/` (pins `assimp`, `freetype`, `draco`
  features — all three must be dropped in the vendored lite build, per
  spec non-goals/goals).
- `find_package(ImGui REQUIRED)` (via `cmake/FindImGui.cmake`, itself
  wrapping `find_package(imgui CONFIG QUIET)`) when
  `ROCKY_SUPPORTS_IMGUI=ON`. Also has a custom overlay port at
  `vcpkg-ports/imgui/`.
- **`slughorn`** (`ROCKY_SUPPORTS_SLUGHORN`, default ON) deserves its own
  flag: `cmake/ResolveSlughorn.cmake` first tries
  `find_package(Slughorn QUIET MODULE)`; if that fails it runs
  `FetchContent_Declare`/`FetchContent_MakeAvailable` pulling a tarball
  **directly from `https://github.com/AlphaPixel/slughorn/archive/...`**
  — this is a live, unconditional network dependency today whenever
  slughorn isn't pre-installed, and it directly violates constraint §2.2
  ("no `FetchContent` that downloads"). **Must default `ROCKY_SUPPORTS_SLUGHORN`
  to OFF in lite mode**; this is a clean existing switch, not a code change.

None of VSG/vsgXchange/ImGui/slughorn currently has vendoring machinery
(no `third_party/` directory exists in the tree at all yet) — Phase 1 is
starting from zero on vendoring, not adapting an existing mechanism.

## 5. Tile layer structure

`[VERIFY]` resolved: GDAL and plain-XYZ/TMS tile sources are **already
separate class hierarchies**, not a single GDAL-backed implementation:
- `TMSImageLayer`/`TMSElevationLayer` (`TMS.{h,cpp}`) — pure HTTP/TMS,
  delegates image *fetch* to `URI::read` and image *decode* to a pluggable
  `io.services().readImageFromStream` hook (see §7) — **no GDAL
  dependency** in this path at all.
- `GDALImageLayer`/`GDALElevationLayer`/`GDALFeatureSource` — separate
  classes, already behind `ROCKY_HAS_GDAL`.
- `MBTilesImageLayer`/`MBTilesElevationLayer` — behind sqlite/zlib, separate
  again.
- `AzureImageLayer`/`BingImageLayer`/`BingElevationLayer` — separate,
  already behind `ROCKY_SUPPORTS_AZURE`/`ROCKY_SUPPORTS_BING` options
  (Azure defaults ON, Bing OFF — flip Azure OFF too in lite, it's unused by
  the example and pulls in nothing extra today but is out of scope).

This means Phase 3's "keep only XYZ/TMS + elevation" requirement is
largely already true structurally; the work is mostly CMake option
flipping plus adding httplib as the default transport (see §6) and a new
local-directory/`file://` tile source (does not exist yet — must be
written).

## 6. HTTP client

`[VERIFY]` resolved: **both** cpp-httplib and curl are already supported,
behind a shared abstraction in `src/rocky/URI.cpp`/`URI.h`, selected at
CMake time via `ROCKY_SUPPORTS_HTTPLIB` (default **OFF**) /
`ROCKY_SUPPORTS_CURL` (default **ON**) — they're mutually exclusive (CMake
warns and disables CURL if both are requested). TLS is controlled
independently by `ROCKY_SUPPORTS_HTTPS` (default ON), which maps to
`CPPHTTPLIB_OPENSSL_SUPPORT` for the httplib backend or is implicit in
curl's own build for the curl backend. `URI::supportsHTTPS()` already
returns `false` cleanly when neither TLS path is compiled in — there is no
code path that silently attempts HTTPS and fails ungracefully.

**This substantially pre-empts Phase 3's HTTP work**: the "minimal httplib
backend behind the existing abstraction" the spec asks for already exists
and is already the non-default option. Lite mode mostly needs
`ROCKY_SUPPORTS_HTTPLIB=ON`, `ROCKY_SUPPORTS_CURL=OFF`,
`ROCKY_SUPPORTS_HTTPS=OFF`, plus vendoring cpp-httplib itself (currently
found via vcpkg's header-only install, `find_path(CPP_HTTPLIB_INCLUDE_DIRS
"httplib.h")`).

## 7. Tile cache / sqlite

`[VERIFY]` resolved, and the spec's assumption was wrong in a helpful way:
**there is no disk tile cache in Rocky at all.** `src/rocky/Cache.h`
defines only in-memory caches (`ResidentCache` — weak-ptr cache of live
objects, `LRUCache` — bounded in-memory LRU). sqlite is used **only** as
an MBTiles tile-source *format* reader/writer (a self-contained `.mbtiles`
file is itself a sqlite database), never as a generic cache layer. So
Phase 3's "disable the cache or replace it with a file-per-tile cache
directory" instruction has nothing to act on — nothing needs replacing.
Section 3's "local plain-directory XYZ tiles (`{z}/{x}/{y}.png`)" layer
described in the spec does not exist yet and will need to be written from
scratch (there's no existing directory-based tile source to adapt).

## 8. Shader compilation

`[VERIFY]` resolved: shaders (`src/rocky/vsg/shaders/*.{vert,frag,comp,glsl}`,
37 files) are **not** compiled via the `glslangValidator` command-line
tool at any point — Rocky's build never invokes it, and it is not a
dependency of any CMake target in this repo (the only `find_package(glslang
...)` call in the whole tree is in `rocky_demo_qt`'s `CMakeLists.txt`,
which is off by default via `ROCKY_SUPPORTS_QT=OFF` and out of scope
anyway). Instead, `VSGContext.cpp` creates a `vsg::ShaderCompileSettings`
and shaders are read at **runtime** through VSG's own shader-reading
machinery, which embeds glslang as a linked library (pulled in
transitively by the `vsg` package) to compile GLSL to SPIR-V on the fly
when a shader is first loaded. So the constraint "`glslangValidator` must
not be required at build time on the target machine" is **already
satisfied in the current design** — nothing in Rocky's own build graph
calls the CLI. The remaining lite-mode work is narrower than the spec
assumed: vendor `glslang` as a library (because VSG's runtime shader
compiler link-depends on it) rather than solve a CLI invocation problem.
Precompiling to `.spv` at dev time remains an available option later but
is not required to satisfy the hard constraint.

## 9. Windowing

`[VERIFY]` confirmed: no GLFW/SDL reference anywhere in `src/`. VSG
provides its own native windowing (Win32 on Windows; XCB on Linux via
`libvulkan-dev`, `libxcb1-dev`, `libx11-xcb-dev`, etc. — see the exact
package list already used by `.github/workflows/ci-linux-minimal.yml`,
which should be copied into `README_LITE.md` verbatim since it's a known-
good set).

## 10. ECS component naming correction

Spec §5.2 says "Prefer Rocky's own ECS components (Transform, Mesh, Line,
Icon, Label, etc.)". `Transform`, `Mesh`, `Line`, `Label` all exist exactly
as named under `src/rocky/ecs/`. **There is no `Icon` component** — the
closest equivalents are `Point` (billboarded point, has an associated
`PointSystem`) and `Texture`/`ProjectedTexture`. The `rocky.icon.vert/frag`
shaders exist but back a different system (likely reached through
`Point`+`Texture`, needs a one-line confirmation when Phase 5 is
implemented, not a blocker now). Everything else named in the spec
(`Model`, `Widget`, `Decal`) also exists.

## 11. Custom vcpkg overlay ports (not in the original spec's assumptions)

`vcpkg-ports/{imgui,slughorn,vsgxchange}/` carry **Pelican Mapping's own
patches/version pins** on top of upstream vcpkg — a `portfile.cmake` and
pinned `vcpkg.json` for each. When vendoring these three for the lite
build, their actual required patch content (not just upstream tarballs)
needs to be diffed and preserved: see `vcpkg-ports/*/portfile.cmake` for
exact source URLs/patches at Phase 1 time.

## 12. Summary — what's gone vs. what already has a switch

| Item | State today |
|---|---|
| GDAL | Optional (`ROCKY_SUPPORTS_GDAL`, default ON) — flip to OFF |
| PROJ | **Not optional** — needs a new option + the SRS replacement |
| sqlite/zlib (MBTiles) | Optional (`ROCKY_SUPPORTS_MBTILES`, default ON) — flip to OFF |
| OpenSSL/TLS | Optional (`ROCKY_SUPPORTS_HTTPS`, default ON) — flip to OFF |
| curl vs. httplib | Both already exist; flip default backend |
| ImGui | Optional, default ON — flip to OFF |
| Slughorn | Optional, default ON, **and is the one component with a live, unconditional network fetch fallback today** — flip to OFF |
| Azure/Bing layers | Already optional (Azure ON by default, Bing OFF) — flip Azure OFF too |
| Disk tile cache | Doesn't exist — nothing to remove |
| `glslangValidator` CLI | Never required at build time already |
| GLFW/SDL | Never present |
| `third_party/` vendoring infra | Does not exist yet — all of Phase 1 is new |
| Local-directory XYZ tile source | Does not exist yet — new code, Phase 3 |

## Recommendation

Per the spec's own stop condition (~150 GDAL/PROJ reference sites), the
raw count is exceeded, but the *reason* for that threshold — fear that the
terrain engine is inextricably wired into GDAL/PROJ internals — does not
hold up: GDAL is already cleanly optional at the CMake level, and PROJ,
while hard-required today, is entirely contained in one file (`SRS.cpp`)
with a clean public interface. **Recommend proceeding to Phase 1** rather
than pivoting to "build directly on VSG." The actual largest body of new
work is not dependency removal (most removals are a CMake option flip)
but **vendoring infrastructure that doesn't exist yet** (`third_party/`,
VSG/vsgXchange/glslang/EnTT/glm/spdlog/nlohmann_json/cpp-httplib/stb_image
all currently come from vcpkg only) and **replacing PROJ in `SRS.cpp`**.

## Phase 1 — Vendoring and top-level CMake (done)

Vendored under `third_party/` (trimmed, pinned copies; see
`third_party/VERSIONS.md` for exact commits/licenses/vendored paths):
glm `1.0.3`, EnTT `v3.13.2`, spdlog `v1.17.0`, nlohmann/json `v3.12.0`,
cpp-httplib `v0.60.1`, `stb_image.h`, cppmap3d `1.3.0` (per the user's
suggestion -- not wired into `SRS.cpp` yet, that's Phase 2), VSG `v1.1.16`,
vsgXchange `v1.1.14`, glslang `vulkan-sdk-1.4.321.0` (library only, no
`glslangValidator`/`spirv-remap` binaries), Dear ImGui `v1.92.5`
(vulkan-binding only).

**Correction to this section's own first draft, made mid-Phase-1 at the
user's request: ImGui is kept ON in `ROCKY_LITE`, not turned off.** §1 of
this document originally filed ImGui next to GDAL/sqlite/curl as "optional,
flip to OFF," matching the spec's framing of it as a prunable demo/debug
dependency. Building with `ROCKY_SUPPORTS_IMGUI=OFF` to validate that plan
immediately exposed that `LabelSystem.cpp` (ECS label rendering) is
implemented *entirely* on top of ImGui draw calls with no non-ImGui
fallback, and was missing the `ROCKY_HAS_IMGUI` guard its sibling
`WidgetSystem` already has (a latent upstream gap -- nobody had hit it
because ImGui defaults ON everywhere today). Rather than accept "labels
silently stop rendering" as the cost of pruning ImGui, the user asked to
keep it. Fixed properly either way: `LabelSystem.{h,cpp}` is now excluded
from the build when ImGui isn't present (`src/rocky/CMakeLists.txt`,
mirroring the existing Slughorn pattern) and its one registration call
site in `ECSNode.cpp` is now guarded like `WidgetSystem`'s already was --
so `ROCKY_SUPPORTS_IMGUI=OFF` is now actually safe to build, even though
the lite default no longer exercises that path. ImGui turned out to cost
nothing toward the "offline, no vcpkg" goal anyway: it's small (~3.7 MB
vendored, vulkan-binding only, no GLFW/SDL/Win32 backend needed since
Rocky feeds VSG's own input events into it directly), MIT-licensed, and
Rocky's own `cmake/FindImGui.cmake` already had a from-source build path
via an `IMGUI_DIR` variable -- so vendoring it needed no custom
`CMakeLists.txt` at all, unlike every other dependency above.

**Deviation from the spec's exact option names (recorded per the spec's
own §0 instruction):** §4 Phase 1 names new options
`ROCKY_WITH_GDAL`/`ROCKY_WITH_PROJ`/`ROCKY_WITH_CURL`/`ROCKY_WITH_OPENSSL`/
`ROCKY_WITH_SQLITE`. Phase 0 found the actual existing option names are
`ROCKY_SUPPORTS_GDAL`/`ROCKY_SUPPORTS_CURL`/`ROCKY_SUPPORTS_HTTPS`/
`ROCKY_SUPPORTS_MBTILES` (no PROJ option exists at all yet). Introducing a
second, differently-named set of options that control the same thing
would just be confusing, so `ROCKY_LITE=ON` (new, default ON) instead
forces the *existing* options to the right lite-mode values: `GDAL` off,
`CURL` off, `HTTPLIB` on, `HTTPS` off, `MBTILES` off, `SLUGHORN` off,
`AZURE` off, `QT` off (`IMGUI` is deliberately left at its existing
default ON -- see below). The code's names win, per the spec's own rule.

**PROJ is still hard-required even with `ROCKY_LITE=ON`** -- there is no
`ROCKY_HAS_PROJ`/`ROCKY_WITH_PROJ` option yet; that lands in Phase 2 along
with the `SRS.cpp` rewrite. So a from-scratch `ROCKY_LITE=ON` configure
today still needs a system PROJ (and, by extension, GDAL's own transitive
PROJ dependency is irrelevant since `ROCKY_SUPPORTS_GDAL` is now off) --
Phase 1's acceptance is "vendoring infra + build options exist and work,"
not yet "builds with zero non-Vulkan system dependencies."

**Build-tree wiring mechanism:** `third_party/CMakeLists.txt` uses
`FetchContent_Declare(<name> SOURCE_DIR <vendored path>
OVERRIDE_FIND_PACKAGE)` for each dependency. Since `SOURCE_DIR` already
points at the vendored copy, this never downloads anything; the
`OVERRIDE_FIND_PACKAGE` redirect means every *existing*
`find_package(<name> ...)` call -- in `src/rocky/CMakeLists.txt`, and
inside VSG's/vsgXchange's own unmodified `CMakeLists.txt` files -- picks
up the vendored copy automatically, with **zero changes needed to those
call sites**. This required bumping `cmake_minimum_required` from `3.21`
to `3.24` (`OVERRIDE_FIND_PACKAGE` needs CMake >= 3.24); still comfortably
within the spec's ">= 3.20" floor. `cpp-httplib` is the one dependency
Rocky locates via `find_path()` rather than `find_package()`, so its
cache variable (`CPP_HTTPLIB_INCLUDE_DIRS`) is pre-seeded directly
instead.

Two build-system wrinkles worth recording for whoever touches this next:
- `CMAKE_SKIP_INSTALL_RULES` is forced `ON` for `ROCKY_LITE` builds.
  Several vendored dependencies (`glm`, `spdlog` built as in-tree
  subprojects rather than found as installed CONFIG packages) don't
  export themselves the way `rocky`'s own `install(EXPORT rockyTargets
  ...)` needs for a redistributable SDK install -- and rocky-lite has no
  goal of producing one, only of building and running the example, so
  install processing is skipped outright rather than solved per
  dependency.
- That flag doesn't cover everything, though: VSG's own CMake helper
  macro additionally does a **build-tree** `export(EXPORT vsgTargets
  ...)` (the bare `export()` command, not `install(EXPORT)`), which
  performs its own export-set completeness check unaffected by
  `CMAKE_SKIP_INSTALL_RULES`. It needs glslang's targets (`glslang`,
  `SPIRV`, `glslang-default-resource-limits`) to be part of *some*
  export set, so `third_party/CMakeLists.txt` eagerly resolves glslang
  and does `install(TARGETS glslang SPIRV glslang-default-resource-limits
  EXPORT vsgTargets)` itself before VSG is configured -- export-set
  membership bookkeeping happens at that `install(TARGETS ... EXPORT)`
  call regardless of `CMAKE_SKIP_INSTALL_RULES`; only the final install
  action is what that flag suppresses.

**Trimming gaps found only by actually configuring/building** (fixed,
now reflected in `third_party/VERSIONS.md`'s vendored-paths column):
glslang needed three root-level files beyond `glslang/`, `SPIRV/`,
`build_overrides/` -- `parse_version.cmake`, `build_info.h.tmpl`,
`CHANGES.md` -- plus, discovered only at compile time, a single header
`StandAlone/DirStackFileIncluder.h` that `glslang_c_interface.cpp` (part
of the *library*, not the CLI tool) includes; vendored that one header
without the rest of `StandAlone/` (which stays excluded --
`ENABLE_GLSLANG_BINARIES=OFF` never adds it as a subdirectory). Similarly,
nlohmann/json's CMake install step needed `nlohmann_json.natvis` from the
repo root, not just `include/`.

**Validated by actually configuring and building**, not just by reading
CMakeLists.txt: a from-scratch MSVC (Visual Studio 17 2022, x64) configure
of `-DROCKY_LITE=ON` against this `third_party/` tree succeeds end to end
(glm, EnTT, spdlog, nlohmann_json, ImGui, VSG, vsgXchange, glslang all
resolve through the vendored copies, zero vcpkg/system lookups for any of
them), and `cmake --build --target rocky` compiles and links the entire
`rocky` library -- including `LabelSystem.cpp`/`WidgetSystem.cpp`/
`ImGuiIntegration.cpp`/`RenderImGui.cpp`/`SendEventsToImGui.cpp` and the
vendored `imgui*.cpp`/`imgui_impl_vulkan.cpp` themselves -- against the
vendored stack with zero errors. This was done with PROJ's `find_package`
block in `src/rocky/CMakeLists.txt` temporarily stubbed out locally
(reverted immediately after -- not committed, confirmed via `git diff`
showing only the intended `LabelSystem`/`ECSNode.cpp` guard changes
remaining) purely to get past the still-hard-required PROJ dependency
Phase 2 will remove; with that one stub in place, the *only* compile
error anywhere in the whole `rocky` target is `SRS.cpp`'s
`#include <proj.h>` itself -- i.e. Phase 1's vendoring is complete and
PROJ in `SRS.cpp` is the sole remaining blocker to a from-scratch
ROCKY_LITE build, exactly as Phase 2 is scoped to address.
