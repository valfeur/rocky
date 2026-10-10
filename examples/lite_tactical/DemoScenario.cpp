/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "DemoScenario.h"
#include <rocky/rocky.h>
#include <random>

using namespace ROCKY_NAMESPACE;

namespace
{
    double wrapDegrees0to360(double d)
    {
        d = fmod(d, 360.0);
        return d < 0.0 ? d + 360.0 : d;
    }
}

DemoScenario::DemoScenario(const Params& params) :
    _params(params)
{
}

void DemoScenario::update(double t, tactical::SimContext& context)
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
    GeoPoint pos = GeoPoint(SRS::ECEF, worldEcef).transform(wgs84);

    _track.id = "TRACK-1";
    _track.label = _params.label;
    _track.position = { pos.x, pos.y, pos.z };

    // Heading: the compass bearing of the velocity vector (0 = North, 90
    // = East). This orbit's angle increases counter-clockwise as seen
    // from above, which is a left-hand turn, so heading decreases as
    // angle increases -- the two are related by a simple sign flip.
    _track.heading_deg = wrapDegrees0to360(-glm::degrees(angle));

    // Bank to match a coordinated turn. Since this is a left-hand turn,
    // the correct bank is left-wing-down, i.e. negative roll.
    _track.roll_deg = -glm::degrees(atan((_params.speed_mps * _params.speed_mps) / (g * _params.radius_m)));
    _track.pitch_deg = 0.0; // level flight
    _track.modelPath = _params.modelPath;

    if (t - _lastTrailSample_t >= trailSampleInterval_s)
    {
        _lastTrailSample_t = t;
        _trailPoints.push_back(_track.position);
        if (_trailPoints.size() > trailMaxPoints)
            _trailPoints.erase(_trailPoints.begin());
    }

    context.tracks = { _track };

    tactical::Trajectory traj;
    traj.id = "TRACK-1-TRAJECTORY";
    traj.trackId = _track.id;
    traj.points = _trailPoints;
    context.trajectories = { traj };

    tactical::FieldOfView fov;
    fov.id = "TRACK-1-RADAR";
    fov.trackId = _track.id;
    fov.range_m = _params.radarRange_m;
    fov.azimuthBeamwidth_deg = _params.radarAzimuthBeamwidth_deg;
    fov.elevationBeamwidth_deg = _params.radarElevationBeamwidth_deg;
    context.fieldsOfView = { fov };

    // A single reference waypoint at the orbit's center, to exercise the
    // Waypoint category -- a real scenario would have mission/route points
    // of its own.
    tactical::Waypoint base;
    base.id = "BASE";
    base.label = "BASE";
    base.position = { _params.centerLon_deg, _params.centerLat_deg, 0.0 };
    context.waypoints = { base };

    // A handful of jittered points near the track, refreshed every couple
    // of seconds, standing in for raw sensor detections that haven't been
    // correlated into a track -- to exercise the Plot category.
    if (t - _lastPlotRefresh_t >= 2.0)
    {
        _lastPlotRefresh_t = t;
        _plots.clear();

        std::mt19937 rng(static_cast<unsigned>(t * 1000.0));
        std::uniform_real_distribution<double> jitter_deg(-0.02, 0.02); // a few km at this latitude

        for (int i = 0; i < 5; ++i)
        {
            tactical::Plot p;
            p.id = "PLOT-" + std::to_string(i);
            p.position.lon_deg = _track.position.lon_deg + jitter_deg(rng);
            p.position.lat_deg = _track.position.lat_deg + jitter_deg(rng);
            p.position.alt_m = _params.altitude_m * 0.5;
            p.timestamp_s = t;
            _plots.push_back(p);
        }
    }
    context.plots = _plots;
}
