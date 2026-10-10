/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "ModelLoader.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Result<MeshGeometry>
ModelLoader::load(const std::string& path, const Config& config)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials; // unused -- see header comment
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());
    if (!ok)
        return Failure(Failure::ResourceUnavailable, "Failed to load " + path + ": " + err);

    auto rotation = quaternion_from_euler_degrees(config.pitch_deg, config.roll_deg, config.heading_deg);
    auto rotationMatrix = glm::dmat3(glm::mat4_cast(rotation));

    MeshGeometry geom;

    for (auto& shape : shapes)
    {
        for (auto& index : shape.mesh.indices)
        {
            glm::dvec3 v(
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]);

            v = (v - config.noseOffset) * config.scale;
            v = rotationMatrix * v;
            geom.vertices.push_back(v);

            if (index.normal_index >= 0)
            {
                glm::dvec3 n(
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]);
                geom.normals.push_back(rotationMatrix * n);
            }
            else
            {
                geom.normals.push_back({ 0, 0, 1 });
            }

            geom.indices.push_back((std::uint32_t)geom.indices.size());
        }
    }

    if (geom.vertices.empty())
        return Failure(Failure::ResourceUnavailable, path + " contained no geometry");

    return geom;
}
