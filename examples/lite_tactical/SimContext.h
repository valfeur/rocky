/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <string>
#include <vector>

// SimContext describes everything this visualization can draw, as plain
// data with no dependency on Rocky, VSG, or any particular rendering
// concept. A third-party application owns a SimContext, fills it in from
// its own data model (tracker output, mission plan, sensor feed, whatever
// it already has), and hands it to SceneSync::sync() once per frame (see
// SceneSync.h). That's the entire integration surface: populate this
// struct, call one function.
//
// Objects are matched across calls to sync() by their `id` field: the
// same id with changed fields updates the existing visual in place; a new
// id creates one; an id that stops appearing is removed. An id only needs
// to be unique within its own vector here (a Track and a Waypoint may
// reuse the same id string without conflict) -- use whatever identifier
// your own data model already has, stringified.
//
// This file is deliberately self-contained and framework-agnostic: it's
// meant to be easy to read top to bottom, and easy to adapt (by hand, or
// by handing this single file to an LLM) to match a specific existing
// data model's field names and units.
namespace tactical
{
    using Id = std::string;

    //! A WGS84 geographic position.
    struct GeoPosition
    {
        double lon_deg = 0.0;
        double lat_deg = 0.0;
        double alt_m = 0.0; // height above the WGS84 ellipsoid, in meters
    };

    //! An RGBA color, each channel in [0..1].
    struct Color
    {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    };

    //! A tracked, moving platform -- the primary object a system is
    //! actively following. Rendered as a 3D model (or, with no modelPath,
    //! a simple procedural placeholder shape) plus a text label.
    struct Track
    {
        Id id;
        std::string label;
        GeoPosition position;
        double heading_deg = 0.0; // compass heading, 0 = north, 90 = east
        double pitch_deg = 0.0;   // nose up/down
        double roll_deg = 0.0;    // bank, positive = right wing down

        //! Path to a 3D model file to render for this track (.obj). Leave
        //! empty to use the built-in procedural placeholder instead --
        //! the only option that needs no external file, so it's what a
        //! new integration should start with.
        std::string modelPath;

        Color color{1.0f, 0.1f, 0.8f, 1.0f}; // vivid, rarely occurs in basemap imagery
    };

    //! The historical (or planned) path of a Track, as an ordered list of
    //! points. Rendered as a polyline.
    struct Trajectory
    {
        Id id;
        Id trackId; // which Track this belongs to (informational only)
        std::vector<GeoPosition> points;
        Color color{1.0f, 1.0f, 0.0f, 1.0f};
        float width_px = 2.0f;
    };

    //! A planned or reference point -- a route or mission waypoint.
    //! Independent of any Track. Rendered as a small marker plus a label.
    struct Waypoint
    {
        Id id;
        std::string label;
        GeoPosition position;
        // White, the obvious "neutral marker" choice, is exactly the
        // wrong one: real basemap styles use white/light-gray for
        // built-up areas, so a white marker vanishes over any town or
        // city -- the same camouflage problem a white or gray track icon
        // has against generic terrain. Cyan, like magenta, essentially
        // never occurs in cartography.
        Color color{0.0f, 1.0f, 1.0f, 1.0f};
    };

    //! A system's field of view -- a radar beam, camera cone, or similar
    //! sensor volume -- attached to and following a Track. Rendered as a
    //! translucent cone from the track's position, oriented by the
    //! track's heading/pitch/roll plus this field of view's own
    //! boresight offset.
    struct FieldOfView
    {
        Id id;
        Id trackId; // which Track this is attached to (required)
        double range_m = 50000.0;
        double azimuthBeamwidth_deg = 10.0;
        double elevationBeamwidth_deg = 10.0;
        double boresightAzimuth_deg = 0.0;   // offset from the track's heading
        double boresightElevation_deg = 0.0; // offset from the track's pitch
        Color color{0.0f, 1.0f, 0.0f, 0.25f};
    };

    //! A single raw detection or contact report -- typically numerous and
    //! short-lived, not (or not yet) correlated into a continuous Track.
    //! Rendered as a small point marker.
    struct Plot
    {
        Id id;
        GeoPosition position;
        double timestamp_s = 0.0; // seconds, in whatever epoch the app uses
        Color color{1.0f, 1.0f, 0.0f, 1.0f};
    };

    //! The complete scene: every object currently being visualized.
    struct SimContext
    {
        std::vector<Track> tracks;
        std::vector<Trajectory> trajectories;
        std::vector<Waypoint> waypoints;
        std::vector<FieldOfView> fieldsOfView;
        std::vector<Plot> plots;
    };
}
