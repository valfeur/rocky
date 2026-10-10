/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "RadarBeam.h"
#include "Primitives.h"

RadarBeam::RadarBeam(Registry& registry, const Params& params) :
    _registry(registry),
    _params(params)
{
    build();
}

void RadarBeam::build()
{
    // A unit-range cone, apex at the local origin, axis along +Y (the
    // same "forward" axis Aircraft uses for its nose). Scaling this by
    // range_m each frame scales the far cap's radii too, so the beam's
    // angular width stays constant regardless of range.
    MeshGeometry geom;
    double halfAz = glm::radians(_params.azimuthBeamwidth_deg * 0.5);
    double halfEl = glm::radians(_params.elevationBeamwidth_deg * 0.5);
    Primitives::appendCone(geom, 1.0, tan(halfAz), tan(halfEl), _params.segments);

    MeshStyle solidStyle;
    solidStyle.color = _params.color;
    solidStyle.writeDepth = false;     // depth write off...
    solidStyle.drawBackfaces = true;   // ...both faces visible...
    solidStyle.transparencyBin = true; // ...and sorted correctly against other translucent geometry.

    // A thin, brighter outline of the same cone so its volume reads
    // clearly against the terrain, per the spec's legibility requirement.
    MeshStyle wireStyle;
    wireStyle.color = Color(_params.color.r, _params.color.g, _params.color.b, 1.0f);
    wireStyle.wireframe = true;
    wireStyle.writeDepth = false;
    wireStyle.drawBackfaces = true;
    wireStyle.transparencyBin = true;

    _registry.write([&](entt::registry& reg)
        {
            _resourceEntity = reg.create();
            auto& g = reg.emplace<MeshGeometry>(_resourceEntity, geom);
            auto& s = reg.emplace<MeshStyle>(_resourceEntity, solidStyle);

            _wireStyleEntity = reg.create();
            auto& ws = reg.emplace<MeshStyle>(_wireStyleEntity, wireStyle);

            _entity = reg.create();
            reg.emplace<Mesh>(_entity, g, s);
            auto& transform = reg.emplace<Transform>(_entity);
            transform.topocentric = true;
            transform.radius = _params.range_m;

            _wireEntity = reg.create();
            reg.emplace<Mesh>(_wireEntity, g, ws);
            auto& wireTransform = reg.emplace<Transform>(_wireEntity);
            wireTransform.topocentric = true;
            wireTransform.radius = _params.range_m;
        });
}

void RadarBeam::update(const Aircraft& aircraft)
{
    auto aircraftRotation = quaternion_from_euler_degrees(
        aircraft.pitch_deg(), aircraft.roll_deg(), aircraft.heading_deg());

    // The boresight offset is applied in the aircraft's own local frame
    // (innermost), then reoriented by the aircraft's attitude (outermost)
    // -- exactly "inherits position and attitude, plus its own local
    // transform for boresight offset" from the spec.
    auto boresightRotation = quaternion_from_euler_degrees(
        _params.boresightElevation_deg, 0.0, _params.boresightAzimuth_deg);

    auto localMatrix =
        glm::mat4_cast(aircraftRotation * boresightRotation) *
        glm::scale(glm::dmat4(1.0), glm::dvec3(_params.range_m));

    _registry.read([&](entt::registry& reg)
        {
            for (auto e : { _entity, _wireEntity })
            {
                auto& transform = reg.get<Transform>(e);
                transform.position = aircraft.position();
                transform.localMatrix = localMatrix;
                transform.dirty(reg);
            }
        });
}
