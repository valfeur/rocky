/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */

// rocky_lite_tactical -- a top-down tactical display: a globe with a
// tracked platform flying a scripted orbit, its trajectory, a radar-style
// field of view, a reference waypoint, and a handful of simulated raw
// detections. The reference example for ROCKY_LITE_SPEC.md section 5: it
// builds and runs with the lite configuration, with no network and no
// data beyond what's compiled in (the plain globe and a procedural
// placeholder shape).
//
// This example exists in two halves, deliberately kept separate:
//   - SimContext.h describes the scene as plain data (tracks,
//     trajectories, waypoints, fields of view, plots) with no dependency
//     on Rocky or rendering. A real application would own one of these,
//     filled in from its own data model, instead of DemoScenario.
//   - SceneSync.h/.cpp is the one-way bridge from a SimContext to Rocky's
//     ECS scene graph: construct it once, call sync(context) every frame.
// DemoScenario.h/.cpp only stands in for a real application's data
// source, computing a scripted flight path; nothing in it talks to
// rendering at all.
//
// Usage:
//   rocky_lite_tactical
//       Runs with no imagery at all -- just the bare globe. This must
//       always work; it's the spec's mandatory "zero arguments, zero
//       network" path.
//
//   rocky_lite_tactical --tiles <url-or-directory>
//       Adds an XYZ image layer, e.g.:
//         --tiles http://localhost:8080/{z}/{x}/{y}.png   (a tile server)
//         --tiles C:/tiles/{z}/{x}/{y}.png                (a local directory, no server at all)
//       Either form already works through Rocky's existing TMSImageLayer
//       and URI machinery -- see docs/ROCKY_LITE_NOTES.md's Phase 3 notes.
//
//   rocky_lite_tactical --model <path/to/model.obj>
//       Uses a real model for the track's airframe instead of the
//       procedural placeholder (see models/README.md -- models are never
//       fetched or committed by this fork; you place one yourself). With
//       no --model, the first *.obj found under models/ is used
//       automatically; if there isn't one, the procedural placeholder is
//       used instead.
//
//   Other flags (all optional, all have sensible defaults):
//     --lon, --lat             orbit center (degrees; default near Paris)
//     --orbit-radius           orbit radius in meters (default 20000)
//     --altitude               orbit altitude in meters (default 8000)
//     --speed                  airspeed in m/s (default 200)
//     --range                  radar range in meters (default 200000)
//     --beamwidth               radar azimuth/elevation beamwidth in degrees (default 4)

#include <rocky/rocky.h>
#include "DemoScenario.h"
#include "SceneSync.h"

using namespace ROCKY_NAMESPACE;

int main(int argc, char** argv)
{
    vsg::CommandLine args(&argc, argv);

    DemoScenario::Params params;
    args.read("--lon", params.centerLon_deg);
    args.read("--lat", params.centerLat_deg);
    args.read("--orbit-radius", params.radius_m);
    args.read("--altitude", params.altitude_m);
    args.read("--speed", params.speed_mps);
    args.read("--model", params.modelPath);
    args.read("--range", params.radarRange_m);
    double beamwidth_deg = params.radarAzimuthBeamwidth_deg;
    if (args.read("--beamwidth", beamwidth_deg))
    {
        params.radarAzimuthBeamwidth_deg = beamwidth_deg;
        params.radarElevationBeamwidth_deg = beamwidth_deg;
    }

    std::string tiles;
    bool hasTiles = args.read("--tiles", tiles);

    rocky::Application app(argc, argv);
    if (!app.ok())
    {
        rocky::Log()->error("Failed to initialize: {}", app.vsgcontext->status.error().message);
        return -1;
    }

    // Imagery is entirely optional (section 5.1): with no --tiles argument,
    // the map simply has no image layer, and Rocky renders the bare WGS84
    // ellipsoid -- a valid, zero-data, zero-network fallback in its own
    // right, satisfying the spec's "must always run with zero arguments"
    // requirement without any extra code here.
    if (hasTiles)
    {
        auto imagery = TMSImageLayer::create();
        imagery->uri = tiles;
        imagery->profile = Profile("spherical-mercator");
        app.mapNode->map->add(imagery);
    }

    DemoScenario scenario(params);
    SceneSync sceneSync(app.registry);

    // Application only creates its default window lazily, on the first
    // frame, unless asked to do it now -- and we need it now, to reach
    // the view's manipulator below.
    app.realize();

    // A tactical display looks straight down by default -- a radar-style
    // top-down view, not an oblique "flying over terrain" angle.
    auto manip = MapManipulator::get(app.display.window(0).view(0).vsgView);
    if (manip)
    {
        Viewpoint vp;
        vp.point = GeoPoint(SRS::WGS84, params.centerLon_deg, params.centerLat_deg, 0.0);
        vp.range = Distance(params.radius_m * 3.0, Units::METERS);
        vp.pitch = Angle(-90.0, Units::DEGREES);
        manip->setViewpoint(vp);
    }

    tactical::SimContext context;

    auto startTime = std::chrono::steady_clock::now();

    while (app.active())
    {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        scenario.update(t, context);
        sceneSync.sync(context);
        app.frame();
    }

    return 0;
}
