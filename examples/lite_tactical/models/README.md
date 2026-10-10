# Aircraft models

This example's aircraft is, by default, a simple procedural shape built
from primitives (a cylinder fuselage plus box wings and tail fins -- see
`../Geometry.h` and `../Aircraft.cpp`). That's intentional: it keeps the
example runnable with **zero arguments and zero network access**, which
is a hard requirement of `ROCKY_LITE_SPEC.md` section 5.1.

If you'd like a more realistic aircraft, you can drop your own Wavefront
`.obj` file in this directory (or point `--model` at one anywhere on
disk) and the example will load it instead.

## What this fork does *not* do

This fork never downloads, fetches, or commits any 3D model to the
repository. That's a deliberate choice, not an oversight:

- The [Tacview public repository](https://github.com/Vyrtuoz/Tacview)
  (`3D Models/Data/Meshes/`) has plenty of plausible aircraft models in
  plain Wavefront OBJ format -- e.g. `FixedWing.F-16.obj` -- but:
  - its licensing is genuinely ambiguous for this purpose: the repo has
    a root `LICENSE.txt` (GPLv3), but its `README.md` describes the
    arrangement as "work stored here can be integrated into official
    Tacview builds... credits will be displayed" -- that reads like a
    one-way contribution to Tacview's own product, not a clear public
    redistribution grant for these specific community-modeled assets,
    and no per-model license or credit file exists either; and
  - the meshes have **no embedded materials or textures** (no
    `mtllib`/`usemtl` at all) -- Tacview applies skins separately, so
    even a perfectly legal copy would render as a flat gray shape, no
    better-looking than the procedural fallback already built in.
- More generally: any model you place here is *your* content, under
  *your* license, on *your* machine -- not something this repository
  should ever take a position on by committing it.

See `docs/ROCKY_LITE_NOTES.md`'s Phase 5 notes for the full reasoning.

## Using your own model

1. Place a `.obj` file in this directory (textures are not required --
   the mesh will render as a plain lit gray shape either way, same as
   the procedural fallback, unless you extend `ModelLoader.cpp` to read
   `tinyobj::material_t` and apply a texture via Rocky's `MeshTexture`
   component).
2. Run the example. With no `--model` argument, it picks the first
   `*.obj` it finds here automatically.
3. **Fix the axis convention.** Every 3D modeling tool and exporter has
   its own convention for which axis is "up" and which is "forward," and
   none of them are likely to match Rocky's (X = right, Y = forward/nose,
   Z = up, at an identity rotation). There's no way to detect this
   automatically, so you'll need to figure out the right correction for
   your specific model, then pass it with `--model-rotate <pitch> <roll>
   <heading>` (degrees) and `--model-scale <factor>` (if your model isn't
   already in meters). For example, a glTF-style export (Y up, -Z
   forward) typically needs roughly `--model-rotate 0 0 180`; a Z-up,
   X-forward export typically needs a 90-degree pitch or roll depending
   on its handedness. Trial and error against the running example is the
   practical way to get this right.
4. **Set the nose offset.** The radar beam (see `../RadarBeam.cpp`)
   emanates from local `(0, 0, 0)` after the rotation and scale above are
   applied. If your model's origin isn't at the nose, pass
   `--model-nose <x> <y> <z>` -- in the model's own raw units, before
   scale/rotation -- to shift it there.

See `ModelLoader.h`/`.cpp` for exactly how these are applied, and
`third_party/VERSIONS.md` for the vendored OBJ parser
([tinyobjloader](https://github.com/tinyobjloader/tinyobjloader), MIT)
this uses -- not `assimp`, which despite being a common assumption is a
large compiled library with its own build, not a header-only one, and is
explicitly out of scope for this fork (see `ROCKY_LITE_SPEC.md`).
