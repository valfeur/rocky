/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/rocky.h>

using namespace ROCKY_NAMESPACE;

// Loads a Wavefront .obj file into a local-space MeshGeometry, for anyone
// who has their own aircraft model to use instead of the procedural
// fallback (ROCKY_LITE_SPEC.md section 5.3). Uses the vendored
// tinyobjloader (third_party/tinyobjloader) rather than assimp, which is
// a full compiled library with its own build and is explicitly out of
// scope for this fork -- see docs/ROCKY_LITE_NOTES.md.
//
// Models are never fetched or committed by this example; you place one
// yourself (see models/README.md) and either pass --model <path> or let
// Aircraft auto-detect the first *.obj it finds under models/.
namespace ModelLoader
{
    //! Corrects for a model's own axis convention and units, since OBJ
    //! exporters disagree wildly on both. Applied once, at load time, so
    //! the result matches Rocky's topocentric-ENU local convention (same
    //! as the procedural airframe): X = right, Y = forward/nose, Z = up.
    struct Config
    {
        double scale = 1.0;

        //! Fixed rotation (degrees) that reorients the model's own axes to
        //! X-right/Y-forward/Z-up, using the same (pitch, roll, heading)
        //! convention as quaternion_from_euler_degrees elsewhere in this
        //! example. For instance, a glTF-style model (Y up, -Z forward)
        //! typically needs roughly heading=180 to turn its forward axis
        //! to match; a Z-up, X-forward model needs a 90-degree pitch or
        //! roll depending on its handedness. There's no way to detect
        //! this automatically, so it defaults to identity and you'll need
        //! to adjust it (via --model-rotate) for whatever model you use.
        double pitch_deg = 0.0;
        double roll_deg = 0.0;
        double heading_deg = 0.0;

        //! Offset (in the model's own units, applied before scaling) from
        //! the model's origin to its nose -- the point the radar beam
        //! should emanate from.
        glm::dvec3 noseOffset{ 0, 0, 0 };
    };

    //! Loads an .obj file and returns it as a local-space MeshGeometry
    //! (no SRS set), ready to use with a Mesh + topocentric Transform
    //! exactly like the procedural airframe.
    Result<MeshGeometry> load(const std::string& path, const Config& config);
}
