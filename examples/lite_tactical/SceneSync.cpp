/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "SceneSync.h"
#include "Primitives.h"
#include "ModelLoader.h"
#include <algorithm>

using namespace ROCKY_NAMESPACE;

namespace
{
    inline rocky::Color toColor(const tactical::Color& c)
    {
        return rocky::Color(c.r, c.g, c.b, c.a);
    }

    inline GeoPoint toGeoPoint(const tactical::GeoPosition& p)
    {
        return GeoPoint(SRS::WGS84, p.lon_deg, p.lat_deg, p.alt_m);
    }

    // Removes entries from `map` whose id is no longer present in
    // `current`, calling destroy(entityOrEntityGroup) for each one first.
    template<typename Objects, typename EntityMap, typename Destroyer>
    void removeStale(const Objects& current, EntityMap& map, Destroyer destroy)
    {
        for (auto it = map.begin(); it != map.end(); )
        {
            bool present = std::any_of(current.begin(), current.end(),
                [&](auto& obj) { return obj.id == it->first; });
            if (!present)
            {
                destroy(it->second);
                it = map.erase(it);
            }
            else ++it;
        }
    }

    // The built-in placeholder shape (ROCKY_LITE_SPEC.md section 5.3): a
    // simple cylinder fuselage plus box wings and tail fins, used for any
    // Track with no modelPath. Local axes (identity Transform, before any
    // rotation): X = right, Y = forward (the nose), Z = up -- the nose
    // sits at the local origin, so a field of view attached to a track's
    // position needs no extra offset to emanate from "the nose".
    MeshGeometry buildProceduralPlaceholder(double length, double radius)
    {
        MeshGeometry geom;

        Primitives::appendCylinderAlongY(geom, 0.0, -length, radius, 16);

        Primitives::appendBox(geom,
            /*center*/ { 0.0, -7.0, 0.0 },
            /*half*/   { 7.0, 1.6, 0.08 });

        Primitives::appendBox(geom,
            { 0.0, -length + 0.8, 0.0 },
            { 2.6, 0.6, 0.06 });

        Primitives::appendBox(geom,
            { 0.0, -length + 1.0, radius + 0.7 },
            { 0.06, 0.8, 0.7 });

        return geom;
    }

    // A unit-range cone, apex at the local origin, axis along +Y. Scaling
    // this by range_m each frame scales the far cap's radii too, so the
    // field of view's angular width stays constant regardless of range.
    MeshGeometry buildFovCone(double azimuthBeamwidth_deg, double elevationBeamwidth_deg, int segments)
    {
        MeshGeometry geom;
        double halfAz = glm::radians(azimuthBeamwidth_deg * 0.5);
        double halfEl = glm::radians(elevationBeamwidth_deg * 0.5);
        Primitives::appendCone(geom, 1.0, tan(halfAz), tan(halfEl), segments);
        return geom;
    }
}

SceneSync::SceneSync(rocky::Registry& registry) :
    _registry(registry)
{
}

void SceneSync::sync(const tactical::SimContext& context)
{
    syncTracks(context.tracks);
    syncTrajectories(context.trajectories);
    syncWaypoints(context.waypoints);
    syncFieldsOfView(context.fieldsOfView, context.tracks);
    syncPlots(context.plots);
}

SceneSync::TrackEntity SceneSync::createTrackEntity(entt::registry& reg, const tactical::Track& track)
{
    const double placeholderLength = 15.0;
    const double placeholderRadius = 0.75;

    MeshGeometry geom;
    bool usingModel = false;

    if (!track.modelPath.empty())
    {
        auto loaded = ModelLoader::load(track.modelPath, {});
        if (loaded.ok())
        {
            geom = std::move(loaded.value());
            usingModel = true;
        }
        else
        {
            Log()->warn("Track \"{}\": {} -- using the procedural placeholder instead", track.id, loaded.error().message);
        }
    }

    if (!usingModel)
        geom = buildProceduralPlaceholder(placeholderLength, placeholderRadius);

    // Bounding radius for culling: the placeholder's known length, or
    // (for a loaded model, whose size isn't known ahead of time) the
    // furthest vertex from the local origin.
    double boundingRadius = placeholderLength;
    if (usingModel)
    {
        boundingRadius = 0.0;
        for (auto& v : geom.vertices)
            boundingRadius = std::max(boundingRadius, glm::length(v));
    }

    MeshStyle style;
    // Gunmetal gray (the "realistic" choice) and even a track-specific
    // basemap-safe color both turn out to be unreliable against a real
    // tile layer -- OpenTopoMap's own road coloring alone collides with
    // most of the vivid palette. Honor the app-supplied color, but keep
    // lighting off so it reads at full, flat brightness regardless of
    // sun angle; legibility over photorealism for a tactical display.
    style.color = toColor(track.color);
    style.lighting = false;

    TrackEntity result;
    result.entity = reg.create();
    auto& g = reg.emplace<MeshGeometry>(result.entity, geom);
    auto& s = reg.emplace<MeshStyle>(result.entity, style);
    reg.emplace<Mesh>(result.entity, g, s);

    auto& transform = reg.emplace<Transform>(result.entity);
    transform.topocentric = true;
    transform.radius = boundingRadius;

    // At a tactical-overview distance a true-to-life few-meter platform
    // is far smaller than a pixel. PixelScale is Rocky's own mechanism
    // for this (used by markers/icons elsewhere): it keeps the entity's
    // bounding-sphere footprint between minPixels and maxPixels by
    // scaling it dynamically each frame, computed from the live camera
    // distance and FOV.
    auto& pixelScale = reg.emplace<PixelScale>(result.entity);
    pixelScale.minPixels = 50.0f;
    pixelScale.maxPixels = 180.0f;

    // Smaller than the 24pt default, and offset above the icon rather
    // than centered on it -- centered, the label sits directly on top of
    // the airframe and hides it instead of complementing it.
    result.labelStyle = reg.create();
    auto& labelStyle = reg.emplace<LabelStyle>(result.labelStyle);
    labelStyle.textSize = 14.0f;
    labelStyle.textOffset = { 0, -28 };

    reg.emplace<Label>(result.entity, track.label, labelStyle);

    return result;
}

void SceneSync::syncTracks(const std::vector<tactical::Track>& tracks)
{
    _registry.write([&](entt::registry& reg)
        {
            removeStale(tracks, _tracks, [&](TrackEntity& e)
                {
                    reg.destroy(e.entity);
                    reg.destroy(e.labelStyle);
                });

            for (auto& track : tracks)
            {
                auto found = _tracks.find(track.id);
                if (found == _tracks.end())
                    found = _tracks.emplace(track.id, createTrackEntity(reg, track)).first;

                auto& transform = reg.get<Transform>(found->second.entity);
                transform.position = toGeoPoint(track.position);
                auto rotation = quaternion_from_euler_degrees(track.pitch_deg, track.roll_deg, track.heading_deg);
                transform.localMatrix = glm::mat4_cast(rotation);
                transform.dirty(reg);

                auto& label = reg.get<Label>(found->second.entity);
                if (label.text != track.label)
                {
                    label.text = track.label;
                    label.dirty(reg);
                }

                auto& style = reg.get<MeshStyle>(found->second.entity);
                auto newColor = toColor(track.color);
                if (style.color != newColor)
                {
                    style.color = newColor;
                    style.dirty(reg);
                }
            }
        });
}

void SceneSync::syncTrajectories(const std::vector<tactical::Trajectory>& trajectories)
{
    _registry.write([&](entt::registry& reg)
        {
            removeStale(trajectories, _trajectories, [&](entt::entity e) { reg.destroy(e); });

            for (auto& traj : trajectories)
            {
                auto found = _trajectories.find(traj.id);
                if (found == _trajectories.end())
                {
                    LineGeometry geom;
                    geom.srs = SRS::WGS84;
                    geom.topology = LineTopology::Strip;

                    LineStyle style;
                    style.color = toColor(traj.color);
                    style.width = traj.width_px;
                    style.widthUnits = Units::PIXELS;

                    auto e = reg.create();
                    auto& g = reg.emplace<LineGeometry>(e, geom);
                    auto& s = reg.emplace<LineStyle>(e, style);
                    reg.emplace<Line>(e, g, s);
                    found = _trajectories.emplace(traj.id, e).first;
                }

                auto& geom = reg.get<LineGeometry>(found->second);
                geom.points.clear();
                geom.points.reserve(traj.points.size());
                for (auto& p : traj.points)
                    geom.points.push_back(toGeoPoint(p));
                geom.dirty(reg);
            }
        });
}

void SceneSync::syncWaypoints(const std::vector<tactical::Waypoint>& waypoints)
{
    _registry.write([&](entt::registry& reg)
        {
            if (_waypointMarkers == entt::null)
            {
                PointGeometry geom;
                geom.srs = SRS::WGS84;

                PointStyle style;
                style.width = 8.0f;
                style.useGeometryColors = true;

                _waypointMarkers = reg.create();
                auto& g = reg.emplace<PointGeometry>(_waypointMarkers, geom);
                auto& s = reg.emplace<PointStyle>(_waypointMarkers, style);
                reg.emplace<Point>(_waypointMarkers, g, s);
            }

            // Waypoints are typically low in number, so rebuilding the
            // shared marker list wholesale each sync is cheap and simple.
            auto& markerGeom = reg.get<PointGeometry>(_waypointMarkers);
            markerGeom.points.clear();
            markerGeom.colors.clear();
            for (auto& wp : waypoints)
            {
                markerGeom.points.push_back(toGeoPoint(wp.position));
                markerGeom.colors.push_back(toColor(wp.color));
            }
            markerGeom.dirty(reg);

            removeStale(waypoints, _waypoints, [&](WaypointEntity& e)
                {
                    reg.destroy(e.entity);
                    reg.destroy(e.labelStyle);
                });

            for (auto& wp : waypoints)
            {
                auto found = _waypoints.find(wp.id);
                if (found == _waypoints.end())
                {
                    auto labelStyleEntity = reg.create();
                    auto& labelStyle = reg.emplace<LabelStyle>(labelStyleEntity);
                    labelStyle.textSize = 12.0f;
                    labelStyle.textOffset = { 0, -16 };

                    auto e = reg.create();
                    reg.emplace<Transform>(e).position = toGeoPoint(wp.position);
                    reg.emplace<Label>(e, wp.label, labelStyle);

                    found = _waypoints.emplace(wp.id, WaypointEntity{ e, labelStyleEntity }).first;
                }
                else
                {
                    auto& transform = reg.get<Transform>(found->second.entity);
                    transform.position = toGeoPoint(wp.position);
                    transform.dirty(reg);

                    auto& label = reg.get<Label>(found->second.entity);
                    if (label.text != wp.label)
                    {
                        label.text = wp.label;
                        label.dirty(reg);
                    }
                }
            }
        });
}

SceneSync::FovEntity SceneSync::createFovEntity(entt::registry& reg, const tactical::FieldOfView& fov)
{
    MeshGeometry geom = buildFovCone(fov.azimuthBeamwidth_deg, fov.elevationBeamwidth_deg, 32);

    // MeshStyle::transparencyBin (the DepthSorted-bin render path in
    // StyleRenderer<T>::traverse(), ECSNode.h) was confirmed by direct
    // testing to make this cone render nothing at all -- not just
    // faint, completely absent -- regardless of color/alpha/writeDepth.
    // Plain alpha blending with no depth-sort (transparencyBin = false)
    // renders correctly. This is a real, reproducible bug in that render
    // path, unrelated to anything specific to this example or to the
    // lite port; since nothing here needs correct back-to-front sorting
    // against OTHER translucent objects (there's only ever one field of
    // view cone in view at a time), the fix here is simply not to use
    // it. See docs/ROCKY_LITE_NOTES.md's Phase 5 notes.
    MeshStyle solidStyle;
    solidStyle.color = toColor(fov.color);
    solidStyle.writeDepth = false;   // don't let the translucent cone occlude things behind it
    solidStyle.drawBackfaces = true; // both faces visible

    // A thin, brighter outline of the same cone so its volume reads
    // clearly against the terrain.
    MeshStyle wireStyle;
    wireStyle.color = Color(fov.color.r, fov.color.g, fov.color.b, 1.0f);
    wireStyle.wireframe = true;
    wireStyle.writeDepth = false;
    wireStyle.drawBackfaces = true;

    FovEntity result;

    result.resource = reg.create();
    auto& g = reg.emplace<MeshGeometry>(result.resource, geom);
    auto& s = reg.emplace<MeshStyle>(result.resource, solidStyle);

    result.wireStyle = reg.create();
    auto& ws = reg.emplace<MeshStyle>(result.wireStyle, wireStyle);

    result.solid = reg.create();
    reg.emplace<Mesh>(result.solid, g, s);
    auto& t1 = reg.emplace<Transform>(result.solid);
    t1.topocentric = true;
    t1.radius = fov.range_m;

    result.wire = reg.create();
    reg.emplace<Mesh>(result.wire, g, ws);
    auto& t2 = reg.emplace<Transform>(result.wire);
    t2.topocentric = true;
    t2.radius = fov.range_m;

    return result;
}

void SceneSync::syncFieldsOfView(const std::vector<tactical::FieldOfView>& fovs, const std::vector<tactical::Track>& tracks)
{
    std::unordered_map<tactical::Id, const tactical::Track*> trackById;
    for (auto& t : tracks)
        trackById[t.id] = &t;

    _registry.write([&](entt::registry& reg)
        {
            removeStale(fovs, _fieldsOfView, [&](FovEntity& e)
                {
                    reg.destroy(e.solid);
                    reg.destroy(e.wire);
                    reg.destroy(e.resource);
                    reg.destroy(e.wireStyle);
                });

            for (auto& fov : fovs)
            {
                auto trackIt = trackById.find(fov.trackId);
                if (trackIt == trackById.end())
                    continue; // orphaned -- nothing to attach to, skip rather than render a stale position

                auto found = _fieldsOfView.find(fov.id);
                if (found == _fieldsOfView.end())
                    found = _fieldsOfView.emplace(fov.id, createFovEntity(reg, fov)).first;

                auto* track = trackIt->second;
                auto trackRotation = quaternion_from_euler_degrees(track->pitch_deg, track->roll_deg, track->heading_deg);
                auto boresightRotation = quaternion_from_euler_degrees(fov.boresightElevation_deg, 0.0, fov.boresightAzimuth_deg);
                auto localMatrix =
                    glm::mat4_cast(trackRotation * boresightRotation) *
                    glm::scale(glm::dmat4(1.0), glm::dvec3(fov.range_m));
                auto pos = toGeoPoint(track->position);

                for (auto e : { found->second.solid, found->second.wire })
                {
                    auto& transform = reg.get<Transform>(e);
                    transform.position = pos;
                    transform.localMatrix = localMatrix;
                    transform.dirty(reg);
                }
            }
        });
}

void SceneSync::syncPlots(const std::vector<tactical::Plot>& plots)
{
    _registry.write([&](entt::registry& reg)
        {
            if (_plotMarkers == entt::null)
            {
                PointGeometry geom;
                geom.srs = SRS::WGS84;

                PointStyle style;
                style.width = 6.0f;
                style.useGeometryColors = true;

                _plotMarkers = reg.create();
                auto& g = reg.emplace<PointGeometry>(_plotMarkers, geom);
                auto& s = reg.emplace<PointStyle>(_plotMarkers, style);
                reg.emplace<Point>(_plotMarkers, g, s);
            }

            // Plots are typically numerous and short-lived -- rebuilt
            // wholesale each sync from whatever the app currently has,
            // rather than id-tracked like the other categories.
            auto& geom = reg.get<PointGeometry>(_plotMarkers);
            geom.points.clear();
            geom.colors.clear();
            for (auto& p : plots)
            {
                geom.points.push_back(toGeoPoint(p.position));
                geom.colors.push_back(toColor(p.color));
            }
            geom.dirty(reg);
        });
}
