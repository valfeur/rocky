/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */

// rocky_lite_tactical -- a 3D globe with an aircraft flying a scripted
// orbit and a translucent radar beam from its nose. The reference example
// for ROCKY_LITE_SPEC.md section 5: it builds and runs with the lite
// configuration, no network access, and no data beyond what's compiled
// in (the plain globe and a procedural airframe).
//
// Usage:
//   rocky_lite_tactical
//       Runs with no imagery at all -- just the bare globe. This must
//       always work; it's the spec's mandatory "zero arguments, zero
//       network" path.
//
//   rocky_lite_tactical --tiles <url-or-directory>
//       Adds an XYZ image layer, e.g.:
//         --tiles http://localhost:8080/{z}/{x}/{y}.png   (a local tile server)
//         --tiles C:/tiles/{z}/{x}/{y}.png                (a local directory, no server at all)
//       Either form already works through Rocky's existing TMSImageLayer
//       and URI machinery -- see docs/ROCKY_LITE_NOTES.md's Phase 3 notes.
//
//   rocky_lite_tactical --model <path/to/model.obj>
//       Uses a real aircraft model instead of the procedural fallback
//       (see models/README.md -- models are never fetched or committed
//       by this fork; you place one yourself). With no --model, the
//       first *.obj found under models/ is used automatically; if
//       there isn't one, the procedural airframe is used instead. A
//       model's own axis convention and units almost never match
//       Rocky's out of the box -- see --model-rotate/--model-scale/
//       --model-nose below, and ModelLoader.h's Config comments.
//
//   Other flags (all optional, all have sensible defaults):
//     --lon, --lat             orbit center (degrees; default near Paris)
//     --orbit-radius           orbit radius in meters (default 20000)
//     --altitude               orbit altitude in meters (default 8000)
//     --speed                  airspeed in m/s (default 200)
//     --range                  radar range in meters (default 50000)
//     --model-scale <s>        uniform scale applied to a loaded model
//     --model-rotate <p r h>   fixed pitch/roll/heading (degrees) correcting
//                              a loaded model's own axis convention
//     --model-nose <x y z>     offset (model units) from its origin to its nose

#include <rocky/rocky.h>
#include "Aircraft.h"
#include "RadarBeam.h"

using namespace ROCKY_NAMESPACE;

int main(int argc, char** argv)
{
    vsg::CommandLine args(&argc, argv);

    Aircraft::Params aircraftParams;
    args.read("--lon", aircraftParams.centerLon_deg);
    args.read("--lat", aircraftParams.centerLat_deg);
    args.read("--orbit-radius", aircraftParams.radius_m);
    args.read("--altitude", aircraftParams.altitude_m);
    args.read("--speed", aircraftParams.speed_mps);

    args.read("--model", aircraftParams.modelPath);
    args.read("--model-scale", aircraftParams.modelConfig.scale);
    args.read("--model-rotate",
        aircraftParams.modelConfig.pitch_deg,
        aircraftParams.modelConfig.roll_deg,
        aircraftParams.modelConfig.heading_deg);
    args.read("--model-nose",
        aircraftParams.modelConfig.noseOffset.x,
        aircraftParams.modelConfig.noseOffset.y,
        aircraftParams.modelConfig.noseOffset.z);

    RadarBeam::Params beamParams;
    args.read("--range", beamParams.range_m);

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

    Aircraft aircraft(app.registry, aircraftParams);
    RadarBeam beam(app.registry, beamParams);

    // Application only creates its default window lazily, on the first
    // frame, unless asked to do it now -- and we need it now, to reach
    // the view's manipulator below.
    app.realize();

    // Start the camera looking at the orbit rather than the default
    // whole-earth view.
    if (auto manip = MapManipulator::get(app.display.window(0).view(0).vsgView))
    {
        Viewpoint vp;
        vp.point = GeoPoint(SRS::WGS84, aircraftParams.centerLon_deg, aircraftParams.centerLat_deg, 0.0);
        vp.range = Distance(aircraftParams.radius_m * 6.0, Units::METERS);
        vp.pitch = Angle(-30.0, Units::DEGREES);
        manip->setViewpoint(vp);
    }

    auto startTime = std::chrono::steady_clock::now();

    while (app.active())
    {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        aircraft.update(t);
        beam.update(aircraft);
        app.frame();
    }

    return 0;
}
