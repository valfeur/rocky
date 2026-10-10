/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "Aircraft.h"
#include "Primitives.h"
#include <filesystem>

namespace
{
    double wrapDegrees0to360(double d)
    {
        d = fmod(d, 360.0);
        return d < 0.0 ? d + 360.0 : d;
    }

    // The mandatory procedural fallback (section 5.3): a simple cylinder
    // fuselage plus box wings and tail fins. Local axes (identity
    // Transform, before any rotation): X = right, Y = forward (the
    // nose), Z = up. The nose sits at the local origin, so a radar beam
    // attached to this entity's position needs no extra offset to
    // emanate from "the nose" (see RadarBeam.cpp) -- a loaded model's
    // noseOffset (ModelLoader::Config) puts it in the same place.
    MeshGeometry buildProceduralAirframe(double fuselageLength, double fuselageRadius)
    {
        MeshGeometry geom;

        // Fuselage: a simple open cylinder from the nose (y=0) to the tail.
        Primitives::appendCylinderAlongY(geom, 0.0, -fuselageLength, fuselageRadius, 16);

        // Main wings, swept back, roughly mid-fuselage.
        Primitives::appendBox(geom,
            /*center*/ { 0.0, -7.0, 0.0 },
            /*half*/   { 7.0, 1.6, 0.08 });

        // Horizontal tailplane.
        Primitives::appendBox(geom,
            { 0.0, -fuselageLength + 0.8, 0.0 },
            { 2.6, 0.6, 0.06 });

        // Vertical tail fin, sitting on top of the fuselage at the tail.
        Primitives::appendBox(geom,
            { 0.0, -fuselageLength + 1.0, fuselageRadius + 0.7 },
            { 0.06, 0.8, 0.7 });

        return geom;
    }

    //! Returns the explicit --model path if one was given, otherwise the
    //! first *.obj found under models/ (the "auto-detect" the spec asks
    //! for), or an empty path if neither exists -- in which case the
    //! caller falls back to the procedural airframe.
    std::string resolveModelPath(const std::string& explicitPath)
    {
        if (!explicitPath.empty())
            return explicitPath;

        std::error_code ec;
        if (std::filesystem::is_directory("models", ec))
        {
            for (auto& entry : std::filesystem::directory_iterator("models", ec))
            {
                if (entry.path().extension() == ".obj")
                    return entry.path().string();
            }
        }
        return {};
    }
}

Aircraft::Aircraft(Registry& registry, const Params& params) :
    _registry(registry),
    _params(params)
{
    buildAirframe();
    buildTrail();
}

void Aircraft::buildAirframe()
{
    const double fuselageLength = 15.0;
    const double fuselageRadius = 0.75;

    MeshGeometry geom;
    bool usingModel = false;

    auto modelPath = resolveModelPath(_params.modelPath);
    if (!modelPath.empty())
    {
        auto loaded = ModelLoader::load(modelPath, _params.modelConfig);
        if (loaded.ok())
        {
            geom = std::move(loaded.value());
            usingModel = true;
            Log()->info("Aircraft: loaded model \"{}\"", modelPath);
        }
        else
        {
            Log()->warn("Aircraft: {} -- using the procedural fallback instead", loaded.error().message);
        }
    }

    if (!usingModel)
        geom = buildProceduralAirframe(fuselageLength, fuselageRadius);

    // Bounding radius for culling: the procedural shape's known length,
    // or (for a loaded model, whose size we don't know ahead of time)
    // the furthest vertex from the local origin.
    double boundingRadius = fuselageLength;
    if (usingModel)
    {
        boundingRadius = 0.0;
        for (auto& v : geom.vertices)
            boundingRadius = std::max(boundingRadius, glm::length(v));
    }

    MeshStyle style;
    style.color = Color(0.75f, 0.76f, 0.78f, 1.0f); // light gunmetal gray
    style.lighting = usingModel ? !geom.normals.empty() : true;

    _registry.write([&](entt::registry& reg)
        {
            _entity = reg.create();
            auto& g = reg.emplace<MeshGeometry>(_entity, geom);
            auto& s = reg.emplace<MeshStyle>(_entity, style);
            reg.emplace<Mesh>(_entity, g, s);

            auto& transform = reg.emplace<Transform>(_entity);
            transform.topocentric = true;
            transform.radius = boundingRadius; // bounding radius, for culling

            // A label keeps the aircraft visible (and identifiable) even
            // when it's too small on screen to make out the model itself
            // -- the spec's "stays visible when zoomed far out" fallback.
            auto& label = reg.emplace<Label>(_entity);
            label.text = _params.label;
        });
}

void Aircraft::buildTrail()
{
    // The trail spans a real geographic area (the whole orbit), so unlike
    // the airframe above, its geometry is absolute WGS84 lon/lat/alt, not
    // local meters -- it has no Transform of its own.
    LineGeometry geom;
    geom.srs = SRS::WGS84;
    geom.topology = LineTopology::Strip;

    LineStyle style;
    style.color = StockColor::Yellow;
    style.width = 2.0f;
    style.widthUnits = Units::PIXELS;

    _registry.write([&](entt::registry& reg)
        {
            _trailEntity = reg.create();
            auto& g = reg.emplace<LineGeometry>(_trailEntity, geom);
            auto& s = reg.emplace<LineStyle>(_trailEntity, style);
            reg.emplace<Line>(_trailEntity, g, s);
        });
}

void Aircraft::update(double t)
{
    const double g = 9.80665; // m/s^2

    double omega = _params.speed_mps / _params.radius_m; // orbit angular rate, rad/s
    double angle = omega * t;

    // Parameterize the orbit in a flat East/North tangent plane centered
    // on (centerLon, centerLat), then project that one point back to
    // geodetic. This is a flat-earth approximation of the circle, which
    // is indistinguishable from a true geodesic circle at the kilometer-
    // scale radii this example uses.
    glm::dvec3 localPos(
        _params.radius_m * cos(angle),  // East
        _params.radius_m * sin(angle),  // North
        _params.altitude_m);

    SRS wgs84 = SRS::WGS84;
    GeoPoint center(wgs84, _params.centerLon_deg, _params.centerLat_deg, 0.0);
    auto& ellipsoid = wgs84.ellipsoid();
    glm::dvec3 centerEcef = ellipsoid.geodeticToGeocentric(center);
    glm::dmat4 enuToEcef = ellipsoid.topocentricToGeocentricMatrix(centerEcef);
    glm::dvec3 worldEcef = glm::dvec3(enuToEcef * glm::dvec4(localPos, 1.0));

    _position = GeoPoint(SRS::ECEF, worldEcef).transform(wgs84);

    // Heading: the compass bearing of the velocity vector (0 = North,
    // 90 = East). This orbit's angle increases counter-clockwise as seen
    // from above, which is a left-hand turn, so heading decreases as
    // angle increases -- the two are related by a simple sign flip.
    _heading_deg = wrapDegrees0to360(-glm::degrees(angle));

    // Bank to match a coordinated turn. Since this is a left-hand turn,
    // the correct bank is left-wing-down, i.e. negative roll (positive
    // roll is right-wing-down; see quaternion_from_euler_degrees's use
    // below, where roll is a right-hand rotation about the nose axis).
    _roll_deg = -glm::degrees(atan((_params.speed_mps * _params.speed_mps) / (g * _params.radius_m)));
    _pitch_deg = 0.0; // level flight

    auto rotation = quaternion_from_euler_degrees(_pitch_deg, _roll_deg, _heading_deg);

    _registry.read([&](entt::registry& reg)
        {
            auto& transform = reg.get<Transform>(_entity);
            transform.position = _position;
            transform.localMatrix = glm::mat4_cast(rotation);
            transform.dirty(reg);

            if (t - _lastTrailSample_t >= trailSampleInterval_s)
            {
                _lastTrailSample_t = t;

                auto& trail = reg.get<LineGeometry>(_trailEntity);
                trail.points.push_back(_position);
                if (trail.points.size() > trailMaxPoints)
                    trail.points.erase(trail.points.begin());
                trail.dirty(reg);
            }
        });
}
