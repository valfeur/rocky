/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/rocky.h>
#include "SimContext.h"
#include <unordered_map>

// SceneSync is the entire bridge between a SimContext (plain application
// data, see SimContext.h) and Rocky's ECS scene graph. It owns nothing
// but a Registry reference and a handful of id-to-entity maps, so it can
// tell, each time sync() is called, which objects are new, which changed,
// and which disappeared -- and create, update, or remove the
// corresponding ECS entities accordingly.
//
// Usage is exactly: construct one with your rocky::Registry, then call
// sync(context) once per frame (or whenever the context changes). Nothing
// else in this example talks to the ECS directly.
class SceneSync
{
public:
    explicit SceneSync(rocky::Registry& registry);

    void sync(const tactical::SimContext& context);

private:
    rocky::Registry _registry;

    // One ECS entity per Track carries its Mesh + Transform + PixelScale
    // + Label together (same pattern as a single visual object with
    // multiple components); labelStyle is a second entity because a
    // LabelStyle must own itself to be referenced by Label's constructor.
    struct TrackEntity { entt::entity entity = entt::null; entt::entity labelStyle = entt::null; };
    std::unordered_map<tactical::Id, TrackEntity> _tracks;

    std::unordered_map<tactical::Id, entt::entity> _trajectories; // Line entities

    struct WaypointEntity { entt::entity entity = entt::null; entt::entity labelStyle = entt::null; };
    std::unordered_map<tactical::Id, WaypointEntity> _waypoints;
    entt::entity _waypointMarkers = entt::null; // one shared Point entity for all waypoint dots

    // A field of view is a translucent cone plus a brighter wireframe
    // outline (for legibility), each needing their own style but sharing
    // one geometry.
    struct FovEntity { entt::entity resource = entt::null; entt::entity wireStyle = entt::null; entt::entity solid = entt::null; entt::entity wire = entt::null; };
    std::unordered_map<tactical::Id, FovEntity> _fieldsOfView;

    entt::entity _plotMarkers = entt::null; // one shared Point entity for all plots, rebuilt wholesale each sync

    void syncTracks(const std::vector<tactical::Track>&);
    void syncTrajectories(const std::vector<tactical::Trajectory>&);
    void syncWaypoints(const std::vector<tactical::Waypoint>&);
    void syncFieldsOfView(const std::vector<tactical::FieldOfView>&, const std::vector<tactical::Track>&);
    void syncPlots(const std::vector<tactical::Plot>&);

    TrackEntity createTrackEntity(entt::registry&, const tactical::Track&);
    FovEntity createFovEntity(entt::registry&, const tactical::FieldOfView&);
};
