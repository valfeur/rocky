/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/ecs/Mesh.h>

using namespace ROCKY_NAMESPACE;

// A handful of procedural primitive builders, used to assemble this
// example's fallback aircraft and its radar beam cone without needing any
// external 3D model -- see ROCKY_LITE_SPEC.md section 5.3's mandatory
// procedural fallback, and 5.4 for the beam geometry.
//
// Each function appends triangles to an existing MeshGeometry (in local,
// un-georeferenced meters) so several shapes can be combined into one
// draw call. Local axes follow Rocky's own topocentric-ENU convention for
// an identity Transform: X = right/East, Y = forward/North, Z = up.
//
// All faces are wound counter-clockwise as seen from outside the shape,
// matching MeshStyle::drawBackfaces's documented convention, so the solid
// aircraft body renders correctly with the default (backface-culled)
// style.
namespace Primitives
{
    //! Appends an axis-aligned box centered at `center`, with the given
    //! half-extents along local X, Y, and Z.
    inline void appendBox(MeshGeometry& geom, const glm::dvec3& center, const glm::dvec3& half)
    {
        glm::dvec3 c[8] = {
            center + glm::dvec3(-half.x, -half.y, -half.z), // 0
            center + glm::dvec3( half.x, -half.y, -half.z), // 1
            center + glm::dvec3( half.x,  half.y, -half.z), // 2
            center + glm::dvec3(-half.x,  half.y, -half.z), // 3
            center + glm::dvec3(-half.x, -half.y,  half.z), // 4
            center + glm::dvec3( half.x, -half.y,  half.z), // 5
            center + glm::dvec3( half.x,  half.y,  half.z), // 6
            center + glm::dvec3(-half.x,  half.y,  half.z)  // 7
        };

        // One quad per face, each already wound CCW as seen from outside.
        const unsigned faces[6][4] = {
            { 4, 5, 6, 7 }, // +Z top
            { 0, 3, 2, 1 }, // -Z bottom
            { 1, 2, 6, 5 }, // +X right
            { 0, 4, 7, 3 }, // -X left
            { 3, 7, 6, 2 }, // +Y front
            { 0, 1, 5, 4 }  // -Y back
        };

        for (auto& f : faces)
        {
            auto base = (std::uint32_t)geom.vertices.size();
            for (int i = 0; i < 4; ++i)
                geom.vertices.push_back(c[f[i]]);
            geom.indices.insert(geom.indices.end(), {
                base, base + 1, base + 2,
                base, base + 2, base + 3 });
        }
    }

    //! Appends an open (uncapped) cylinder running along the local Y axis
    //! from y0 to y1, with the given radius.
    inline void appendCylinderAlongY(MeshGeometry& geom, double y0, double y1, double radius, int segments)
    {
        auto addRing = [&](double y)
            {
                auto start = (std::uint32_t)geom.vertices.size();
                for (int i = 0; i < segments; ++i)
                {
                    double a = glm::two_pi<double>() * i / segments;
                    geom.vertices.push_back({ radius * cos(a), y, radius * sin(a) });
                }
                return start;
            };

        std::uint32_t ring0 = addRing(y0);
        std::uint32_t ring1 = addRing(y1);

        for (int i = 0; i < segments; ++i)
        {
            std::uint32_t j = (i + 1) % segments;
            geom.indices.insert(geom.indices.end(), {
                ring0 + i, ring1 + i, ring1 + j,
                ring0 + i, ring1 + j, ring0 + j });
        }
    }

    //! Appends an elliptical cone with its apex at the local origin,
    //! extending along +Y to `length`. The far cap is an ellipse with
    //! half-width `radiusX` and half-height `radiusZ`, and is included so
    //! the shape reads clearly even when viewed nearly along its axis.
    inline void appendCone(MeshGeometry& geom, double length, double radiusX, double radiusZ, int segments)
    {
        std::uint32_t apex = (std::uint32_t)geom.vertices.size();
        geom.vertices.push_back({ 0, 0, 0 });

        std::uint32_t ring = (std::uint32_t)geom.vertices.size();
        for (int i = 0; i < segments; ++i)
        {
            double a = glm::two_pi<double>() * i / segments;
            geom.vertices.push_back({ radiusX * cos(a), length, radiusZ * sin(a) });
        }

        for (int i = 0; i < segments; ++i)
        {
            std::uint32_t j = (i + 1) % segments;
            geom.indices.insert(geom.indices.end(), { apex, ring + i, ring + j });
        }

        // Far cap, fanned from the first ring vertex. Note the reversed
        // (i+1, i) order relative to the side wall above: the cap's
        // outward normal points forward (+Y), the opposite sense of the
        // side wall's outward-and-around normal, so it needs the opposite
        // winding.
        for (int i = 1; i < segments - 1; ++i)
        {
            geom.indices.insert(geom.indices.end(), {
                ring, ring + (std::uint32_t)i + 1, ring + (std::uint32_t)i });
        }
    }
}
