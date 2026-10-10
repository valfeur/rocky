/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/rocky.h>
#include "Aircraft.h"

using namespace ROCKY_NAMESPACE;

// A translucent cone representing a radar beam, attached to an aircraft's
// nose and following its position and attitude every frame, plus its own
// local boresight offset (ROCKY_LITE_SPEC.md section 5.4).
class RadarBeam
{
public:
    struct Params
    {
        double range_m = 50000.0;
        double azimuthBeamwidth_deg = 10.0;
        double elevationBeamwidth_deg = 10.0;
        double boresightAzimuth_deg = 0.0;
        double boresightElevation_deg = 0.0;
        Color color = Color(0.0f, 1.0f, 0.0f, 0.25f);
        int segments = 32;
    };

    RadarBeam(Registry& registry, const Params& params);

    //! Follows the given aircraft's current position and attitude.
    void update(const Aircraft& aircraft);

private:
    Registry _registry;
    Params _params;

    entt::entity _resourceEntity = entt::null; // shared cone MeshGeometry + translucent MeshStyle
    entt::entity _wireStyleEntity = entt::null; // brighter wireframe MeshStyle, same geometry
    entt::entity _entity = entt::null;          // the translucent cone itself
    entt::entity _wireEntity = entt::null;      // a brighter outline, for legibility against the terrain

    void build();
};
