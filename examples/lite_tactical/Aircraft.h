/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/rocky.h>
#include "ModelLoader.h"

using namespace ROCKY_NAMESPACE;

// A single aircraft flying a scripted circular orbit, banking into the
// turn like a real airplane (ROCKY_LITE_SPEC.md section 5.2).
//
// The airframe is either a real model you supply (see models/README.md
// and ModelLoader.h) or, if none is found, the mandatory procedural
// fallback: a cylinder fuselage plus box wings and tail fins (section
// 5.3), built once from Geometry.h's primitives. Models are never
// fetched or committed by this fork -- the procedural shape is what
// guarantees the example always runs with zero arguments and zero
// network, per the spec's own requirement.
//
// Everything is expressed with Rocky's own ECS components (Transform,
// Mesh, Line, Label) rather than raw VSG nodes, and placed with a single
// topocentric Transform per entity -- no hand-rolled ENU/ECEF matrix
// code is needed for that part; Transform::topocentric already builds
// the local-tangent-plane-to-world matrix from the SRS's ellipsoid.
class Aircraft
{
public:
    struct Params
    {
        double centerLon_deg = 2.4;    // orbit center, near Paris by default
        double centerLat_deg = 49.0;
        double radius_m = 20000.0;     // orbit radius
        double altitude_m = 8000.0;
        double speed_mps = 200.0;
        std::string label = "AIRCRAFT";

        //! Explicit model path (--model). If empty, the first *.obj found
        //! under models/ is used instead; if there isn't one, Aircraft
        //! falls back to the mandatory procedural airframe.
        std::string modelPath;
        ModelLoader::Config modelConfig;
    };

    Aircraft(Registry& registry, const Params& params);

    //! Advances the flight to time t (seconds since the example started)
    //! and updates the aircraft's Transform and track trail accordingly.
    void update(double t);

    //! Current state, so other entities (the radar beam) can follow this
    //! aircraft without re-deriving the flight path themselves.
    const GeoPoint& position() const { return _position; }
    double heading_deg() const { return _heading_deg; }
    double pitch_deg() const { return _pitch_deg; }
    double roll_deg() const { return _roll_deg; }

private:
    Registry _registry;
    Params _params;

    entt::entity _entity = entt::null;      // airframe: Transform + Mesh + Label
    entt::entity _trailEntity = entt::null; // track trail: Line (absolute lon/lat/alt)

    GeoPoint _position{ SRS::WGS84 };
    double _heading_deg = 0.0;
    double _pitch_deg = 0.0;
    double _roll_deg = 0.0;

    double _lastTrailSample_t = -1.0e9;
    static constexpr double trailSampleInterval_s = 1.0;
    static constexpr std::size_t trailMaxPoints = 200;

    void buildAirframe();
    void buildTrail();
};
