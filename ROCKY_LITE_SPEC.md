# Specification: rocky-lite (offline, CMake-only fork of Rocky)

## 0. How to use this document

You are Claude Code working in a fork of https://github.com/pelicanmapping/rocky.
This spec describes the goal, constraints, phases and acceptance criteria.

**Important: several statements in this spec about Rocky's internals are
assumptions made from memory and are marked `[VERIFY]`.** Before changing
anything, do Phase 0 (audit) and write down what is actually true. If reality
contradicts the spec, follow the *goals and constraints*, not the assumptions,
and record the deviation in `docs/ROCKY_LITE_NOTES.md`.

## 1. Goal

Make Rocky buildable and runnable in a **fully offline environment** using
**only CMake + a C++ compiler + the Vulkan SDK**, with **no vcpkg** (or any
other package manager) and a **drastically reduced dependency set**.

Deliver a working example: a 3D globe with an aircraft flying a path and a
translucent radar beam emanating from its nose.

The aim is the **minimum amount of change** needed to get there. This is a
dependency-pruning exercise, not a rewrite. Prefer CMake options and small
`#if` guards over deleting or restructuring code, so upstream merges stay
feasible.

### Non-goals

- No ZeroMQ / protobuf work (a later phase; do not add these now, but do not
  make choices that block them).
- No new GIS features. No projections other than those listed in section 3.
- No HTTPS/TLS support.
- No performance work beyond not regressing.

## 2. Hard constraints

1. **Build system:** CMake (>= 3.20) only. `cmake -S . -B build && cmake --build build`
   must work with no extra arguments and **no network access**.
2. **No vcpkg**, no Conan, no `FetchContent` that downloads, no `find_package`
   for anything except Vulkan (and optionally Threads). Remove `vcpkg.json`
   usage from the default path (it may stay in the tree, unused, if removal
   would create merge noise).
3. **All third-party code vendored** under `third_party/<name>/`, built via
   `add_subdirectory` or a small hand-written `CMakeLists.txt` wrapper.
   Pin exact versions/commits in `third_party/VERSIONS.md` with upstream URL and
   license for each.
4. **System requirement: Vulkan SDK only** (loader + headers). `glslangValidator`
   must not be required at build time on the target machine (see 4.4).
5. **Removed entirely from the default build:** GDAL, PROJ, sqlite (unless
   section 3 says otherwise), OpenSSL, curl, zlib, brotli, any vsgXchange
   backend that is not explicitly kept.
6. Windows (MSVC) and Linux (GCC/Clang) must both work. No platform-specific
   package-manager assumptions.
7. License hygiene: Rocky is MIT; keep all upstream license files; every
   vendored dependency must be listed with its license.

## 3. Coordinate / projection scope

Rocky's `SRS` class likely wraps PROJ `[VERIFY]`. Replace that with a small
built-in implementation supporting **only**:

| Name | Notes |
|---|---|
| WGS84 geodetic (lon, lat, alt) | EPSG:4326 |
| WGS84 ECEF geocentric | EPSG:4978 |
| Spherical/Web Mercator | EPSG:3857, needed for typical XYZ tiles |
| Local tangent plane (ENU) at an origin | for entity-local math |

Requirements:
- Closed-form math, `double` precision, no tables, no data files.
- Anything else must **fail loudly** (clear error status / log message), never
  silently return garbage.
- Keep the public `SRS` API shape as close as possible to the original so
  callers compile unchanged. Implement the PROJ-backed parts behind a
  compile-time switch, e.g. `ROCKY_HAS_PROJ` (default OFF), rather than
  deleting them.
- Add unit tests: round-trip geodetic<->ECEF (error < 1e-6 m), known points
  (e.g. lon 0/lat 0/alt 0 -> (6378137, 0, 0)), Mercator round trip, ENU basis
  orthonormality.

## 4. Work plan

### Phase 0 – Audit (no functional changes)

Produce `docs/ROCKY_LITE_NOTES.md` containing:
1. The full dependency list actually used by Rocky's CMake and `vcpkg.json`,
   with, for each: purpose, how it is consumed (header-only / static / shared),
   and whether it is in the keep or remove list below.
2. A count and file list of references to GDAL, PROJ, sqlite, curl/OpenSSL,
   zlib. This is the effort estimate; report it before proceeding.
3. How Rocky obtains VSG, vsgXchange, and (if present) vsgImGui `[VERIFY]`
   (vcpkg? FetchContent? find_package?).
4. How tile layers are structured (which classes depend on GDAL, which are
   pure XYZ/TMS over HTTP) `[VERIFY]`.
5. What the HTTP client is (cpp-httplib? curl?) `[VERIFY]` and whether TLS is
   enabled.
6. Whether a tile cache depends on sqlite or another DB `[VERIFY]`.
7. Which shaders are compiled at runtime and how `[VERIFY]` (see 4.4).

**Stop and summarise findings before Phase 1 if the number of GDAL/PROJ
reference sites exceeds ~150 or if the terrain engine is deeply coupled to
them.** In that case propose an alternative (e.g. building directly on VSG).

### Phase 1 – Vendoring and top-level CMake

- Vendor VSG (and vsgXchange only if needed, see below) into `third_party/`.
- Vendor the light header-only dependencies Rocky needs (candidates, `[VERIFY]`
  against Phase 0: EnTT, glm, spdlog, nlohmann_json, cpp-httplib, stb).
- Top-level `CMakeLists.txt` options:
    - `ROCKY_LITE` (ON by default): enables all the behaviour in this spec.
    - `ROCKY_WITH_GDAL`, `ROCKY_WITH_PROJ`, `ROCKY_WITH_CURL`,
      `ROCKY_WITH_OPENSSL`, `ROCKY_WITH_SQLITE`: all OFF in lite mode.
- Rocky must build against the vendored copies with
  `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` semantic: nothing is downloaded.
- Provide a CI-style script `scripts/offline_build_check.sh` / `.ps1` that
  builds in a clean directory, and (where possible) with the network blocked,
  and fails if any dependency lookup touches the network or a system path
  other than Vulkan.

### Phase 2 – Remove GDAL / PROJ

- Guard or exclude GDAL-based layers and tools behind `ROCKY_WITH_GDAL`.
- Replace SRS per section 3.
- Remove or guard anything that needs PROJ's datum/grid database files.
- Result: the core library and the example in Phase 5 build with GDAL and
  PROJ completely absent from the machine.

### Phase 3 – HTTP and tile sources

Scope: **plain `http://` only**, to an offline tile server on localhost/LAN.

- Use cpp-httplib (vendored) without `CPPHTTPLIB_OPENSSL_SUPPORT`, and without
  zlib/brotli support. If Rocky currently uses curl, add a minimal httplib
  backend behind the existing abstraction and make it the lite default.
  `https://` URLs must produce a clear "TLS not supported in rocky-lite" error.
- Keep only the **XYZ/TMS image layer** and an **elevation layer** that works
  on tiles decodable without GDAL. Preferred elevation encoding: Terrain-RGB
  style PNG (height = -10000 + (R*65536 + G*256 + B) * 0.1) so the same PNG
  decoder is used. Make the encoding configurable via a small enum; document
  the default.
- Decode PNG/JPEG with `stb_image` (vendored). Drop other image backends.
- Tile cache: if it depends on sqlite, either disable it in lite mode or
  replace it with a trivial file-per-tile cache directory. Cache is optional;
  the tile server is local.
- Also support **local `file://` or plain-directory XYZ tiles**
  (`{z}/{x}/{y}.png`) so the example works with no server at all.
- Drop WMS/WMTS/ArcGIS/Bing/GDAL layers from the lite build (guard, don't
  delete, if they are separable).

### Phase 4 – Shaders and windowing

- If shaders are compiled from GLSL at runtime via glslang `[VERIFY]`:
    - Preferred: vendor glslang and build it as part of the tree, **or**
    - Precompile to SPIR-V at development time, commit the `.spv` files (or
      generated C arrays) and load those in lite mode, with a CMake option to
      regenerate them when `glslangValidator` is available.
    - Whichever is chosen, a clean offline build must not need `glslangValidator`
      on the PATH.
- Windowing: VSG has native windowing (Win32/XCB) `[VERIFY]`; do not introduce
  GLFW/SDL. On Linux, document the required system X11/XCB dev packages
  (these are the only additional system packages permitted, and must be listed
  in the README).
- vsgImGui / ImGui: keep only if the example or Rocky's core needs it. If kept,
  vendor it. If optional, put it behind `ROCKY_WITH_IMGUI` (default OFF in lite).

### Phase 5 – Example: flying plane with radar beam

Create `examples/lite_tactical/` (target `rocky_lite_tactical`), buildable with
the lite configuration, no network, no external data other than bundled assets.

#### 5.1 Scene

- A globe (ellipsoid, WGS84) with:
    - an XYZ imagery layer if a `--tiles <url-or-dir>` argument is given;
    - otherwise a **built-in fallback** that needs no data: a procedurally
      generated or bundled low-resolution (<= 2 MB) world image, or a plain
      colored globe with a lat/lon graticule. The example must always run with
      zero arguments and zero network.
- One aircraft entity.
- One radar beam attached to the aircraft's nose.
- Mouse camera navigation (Rocky's default manipulator is fine).

#### 5.2 Aircraft

- Moves along a scripted path, computed on the CPU each frame from time `t`:
  a racetrack or circular orbit at a configurable position, altitude
  (default 8000 m), speed (default 200 m/s), centred near an arbitrary
  default location (e.g. lon 2.4 E, lat 49.0 N).
- Attitude from **heading / pitch / roll** in the local ENU frame, converted
  to a model matrix:
  `M = T(ecef_position) * R_enu_to_ecef(lon, lat) * R_hpr(heading, pitch, roll)`
  Convention (document it in code): body axes **X forward, Y right, Z down**
  (NED-like body in an ENU-derived local frame; be explicit about the
  conversion). In an orbit, bank angle should match a coordinated turn
  (`roll = atan(v^2 / (g * r))`) so the plane visibly banks.
- Use double precision for positions; handle floating-origin/camera-relative
  rendering the way Rocky normally does for entity transforms `[VERIFY: use
  Rocky's own Transform component rather than hand-rolling]`.
- Prefer Rocky's own ECS components (Transform, Mesh, Line, Icon, Label, etc.)
  over raw VSG nodes `[VERIFY: names]`. Use raw VSG only where Rocky has no
  suitable component, and say so in a comment.

#### 5.3 Aircraft 3D model

- Source: the Tacview public repository, meshes folder:
  https://github.com/Vyrtuoz/Tacview/tree/master/3D%20Models/Data/Meshes
  (raw files: `https://raw.githubusercontent.com/Vyrtuoz/Tacview/master/3D%20Models/Data/Meshes/<file>`).
  Note the spaces in the path (`3D Models`), so quote/URL-encode it in scripts.
  The format of the files is unverified `[VERIFY: believed to be Wavefront
  .obj plus textures; list the folder, pick one fighter-sized aircraft, and
  record the actual format, units, axis convention and nose direction]`.
  The folder listing could not be fetched automatically (GitHub blocks
  automated access), so a human or an authenticated `git clone` / `git
  sparse-checkout` of that folder is needed to inspect it.
- **Licensing: the repo README says its content may be integrated into
  official Tacview builds, but it does not state a redistribution license for
  the assets, and no LICENSE file was seen `[VERIFY: look for a LICENSE file
  and per-model credits]`.** Treat the models as NOT redistributable by default:
  **do not commit them to this fork.** Instead:
    - add `scripts/fetch_model.sh|.ps1` that the *user* runs while online
      (sparse-checkout or raw download of one named model plus its textures
      into `models/`), with `models/` in `.gitignore`;
    - add `models/README.md` explaining where to put the files for an
      air-gapped machine (copy the folder over manually);
    - keep the procedural fallback below as the default, and make the example
      auto-detect `models/<name>.*` at startup (`--model <path>` overrides).
    - if a license permitting redistribution is found, record it in
      `third_party/VERSIONS.md` and the model may then be committed.
- Loading: do **not** pull in assimp. In order of preference:
    1. vsgXchange's built-in OBJ reader if one exists in the vendored version
       `[VERIFY]` (it has no heavy deps);
    2. a small vendored OBJ loader (e.g. tinyobjloader, single header, MIT) that
       produces a VSG/Rocky mesh with positions, normals, UVs, per-material
       diffuse color/texture (textures via stb_image);
    3. if the model is too complex, decimate/convert it offline to a simple
       format and commit that (license permitting).
- Models may use arbitrary axis conventions and units. Add a small per-model
  config (scale, rotation to X-forward/Z-down, nose offset) in
  `models/models.json` or in code, so the nose position is known and the beam
  origin can be placed there.
- **Procedural fallback (mandatory):** if no model file is found, generate a
  simple plane from primitives (fuselage cylinder/ellipsoid, swept wing and
  tail as flat boxes/quads) so the example always works. Also add a large
  `Icon`/label fallback so the aircraft stays visible when zoomed far out.

#### 5.4 Radar beam

- A **translucent cone/pyramid** mesh whose apex is at the plane's nose and
  whose axis points along body +X (forward).
- Parameters (struct + CLI flags): `range_m` (default 50 km),
  `azimuth_beamwidth_deg` and `elevation_beamwidth_deg` (default 10 and 10;
  equal values give a circular cone), `boresight_azimuth_deg` /
  `boresight_elevation_deg` offsets relative to the nose (default 0 and 0),
  colour and alpha (default green, 0.25).
- Geometry: apex + an elliptical (or rectangular) far cap tessellated into
  N segments (default 32), side faces plus optional far cap; unit-range
  geometry scaled by `range_m` via a transform so the range can change
  without rebuilding the mesh.
- Rendering requirements:
    - Alpha blending enabled; **depth write disabled, depth test enabled**;
      drawn after opaque geometry (correct bin/ordering in Rocky/VSG); render
      both faces (no back-face culling) or draw back faces then front faces.
    - A thin brighter wireframe/outline of the cone edges so the volume is legible
      against the terrain.
    - Must not clip against the near plane weirdly at oblique angles; test by
      orbiting the camera around the plane.
- The beam is a **child of the aircraft transform** so it inherits position
  and attitude, plus its own local transform for boresight offset.
- Optional (nice-to-have, only if trivial): a sweeping boresight (azimuth
  scan back and forth +/- 60 deg at configurable rate), toggled by `--scan`.
- Controls: keyboard `B` toggles the beam, `+`/`-` change range, `T`
  toggles a trail (past positions as a polyline, ~200 points max).

#### 5.5 Track trail (small, since tracks are in the long-term goal)

- A polyline trail behind the aircraft using Rocky's line component,
  sampled at 1 Hz, capped length. This exercises the line path in the lite
  build.

### Phase 6 – Documentation

- `README_LITE.md`: prerequisites (compiler, CMake, Vulkan SDK, Linux xcb
  dev packages), build commands for Windows and Linux, how to run the example,
  how to pass tiles, how to set up an offline tile server (point to the
  user's choice, don't bundle one).
- `third_party/VERSIONS.md` as described.
- A short "what was removed and why" section in `docs/ROCKY_LITE_NOTES.md`,
  and a "how to merge upstream" section (which files carry lite-specific
  changes).

## 5. Dependency end state (lite build)

Keep (vendored, built from source by the top-level CMake):
VSG; glslang or precompiled SPIR-V; cpp-httplib (no TLS); stb_image;
EnTT, glm, spdlog (or Rocky's current logging), nlohmann_json (only those
Rocky actually uses); an OBJ loader (only if needed); optionally ImGui.

System: compiler, CMake, Vulkan SDK; X11/XCB dev packages on Linux.

Gone: GDAL, PROJ, sqlite (unless the cache needs it, see 3), curl, OpenSSL,
zlib, brotli, assimp, vsgXchange backends beyond what is used, vcpkg.

## 6. Acceptance criteria

1. On a clean machine with only a compiler, CMake and the Vulkan SDK, **with
   networking disabled**, these commands succeed:
   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release -j
   ```
2. `ldd` / `dumpbin /dependents` on the example shows no GDAL, PROJ, curl,
   OpenSSL or sqlite.
3. `./rocky_lite_tactical` with no arguments opens a window showing the globe,
   the plane flying its path and banking in turns, with a visible translucent
   beam from the nose that follows the plane's attitude.
4. `./rocky_lite_tactical --tiles http://localhost:8080/{z}/{x}/{y}.png` and
   `--tiles /path/to/dir` display imagery when available, and fail gracefully
   (log + fallback globe) when not.
5. Unit tests for the SRS replacement pass (`ctest`).
6. `scripts/offline_build_check` passes.
7. `git diff` against upstream is mostly CMake files, `third_party/`, guarded
   code (`#if ROCKY_WITH_*`), the SRS replacement and the new example.
8. `docs/ROCKY_LITE_NOTES.md` lists every `[VERIFY]` item and what was found.

## 7. Working rules for Claude Code

- Start with Phase 0 and report. Then proceed phase by phase, committing after
  each phase with a clear message, keeping the build green at each commit
  (the default configuration should build after every phase).
- Prefer the smallest change that satisfies the constraints. When in doubt,
  guard with an option rather than delete.
- Do not download anything during the build. If you need a third-party source
  that is not in the tree and you have no network access, stop and tell the
  user exactly what to place where.
- Do not invent API names. Read the headers in the repo; if the spec's names
  differ from the code, the code wins.
- If a requirement cannot be met as written (for example the terrain engine
  hard-requires PROJ), stop, explain, and propose the least invasive
  alternative rather than silently working around it.
