/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include "SimContext.h"
#include <vector>

// DemoScenario stands in for a real application's own data source. It
// knows nothing about rendering or Rocky -- it only computes a scripted
// flight path and writes the result into a tactical::SimContext each
// frame, exactly as a real integration would from its own tracker,
// mission plan, or sensor feed. It exists to exercise every category
// SimContext defines (see SimContext.h) with simple, readable math, not
// as a realistic flight or radar model.
class DemoScenario
{
public:
    struct Params
    {
        double centerLon_deg = 2.4;    // orbit center, near Paris by default
        double centerLat_deg = 49.0;
        double radius_m = 20000.0;     // orbit radius
        double altitude_m = 8000.0;
        double speed_mps = 200.0;
        std::string label = "TRACK-1";

        //! Optional 3D model for the track's airframe (.obj). Empty uses
        //! the built-in procedural placeholder -- see SceneSync.h.
        std::string modelPath;

        double radarRange_m = 200000.0;
        double radarAzimuthBeamwidth_deg = 4.0;
        double radarElevationBeamwidth_deg = 4.0;
    };

    explicit DemoScenario(const Params& params);

    //! Advances the scenario to time t (seconds since the example
    //! started) and (re)writes its objects into `context`.
    void update(double t, tactical::SimContext& context);

    //! The track's current position, so main.cpp can point the initial
    //! camera at it without waiting for the first update().
    const tactical::GeoPosition& trackPosition() const { return _track.position; }

private:
    Params _params;
    tactical::Track _track;

    std::vector<tactical::GeoPosition> _trailPoints;
    double _lastTrailSample_t = -1.0e9;
    static constexpr double trailSampleInterval_s = 1.0;
    static constexpr std::size_t trailMaxPoints = 200;

    std::vector<tactical::Plot> _plots;
    double _lastPlotRefresh_t = -1.0e9;
};
