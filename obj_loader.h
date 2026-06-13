#pragma once
#include <simd/simd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include "scene.h"
using namespace simd;
using namespace std;

inline void loadOBJ(const string& filePath, Scene& scene, int materialIndex, float3 offset = float3{0,0,0}, float scale = 1.0f) {
    ifstream file(filePath);
    if (!file) {
        cerr << "Failed to open OBJ file: " << filePath << "\n";
        return;
    }

    vector<float3> vertices;
    int firstTriangleIndex = (int)scene.triangles.size();
    float3 boundsMin = float3(1e30f);
    float3 boundsMax = float3(-1e30f);

    string line;
    while (getline(file, line)) {
        istringstream ss(line);
        string prefix;
        ss >> prefix;

        if (prefix == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            vertices.push_back(float3{x, y, z} * scale + offset);
        } else if (prefix == "f") {
            vector<int> faceIndices;
            string token;
            while (ss >> token) {
                int slashPos = (int)token.find('/');
                string vStr = (slashPos == (int)string::npos) ? token : token.substr(0, slashPos);
                int idx = stoi(vStr);
                if (idx < 0) idx = (int)vertices.size() + idx; // negative = relative to end
                else idx = idx - 1; // OBJ indices are 1-based
                faceIndices.push_back(idx);
            }

            // fan-triangulate polygons with more than 3 vertices
            for (size_t i = 1; i + 1 < faceIndices.size(); i++) {
                Triangle t = {};
                t.p1 = vertices[faceIndices[0]];
                t.p2 = vertices[faceIndices[i]];
                t.p3 = vertices[faceIndices[i + 1]];
                t.materialIndex = materialIndex;
                scene.triangles.push_back(t);

                boundsMin = simd::min(boundsMin, simd::min(simd::min(t.p1, t.p2), t.p3));
                boundsMax = simd::max(boundsMax, simd::max(simd::max(t.p1, t.p2), t.p3));
            }
        }
    }

    Mesh mesh = {};
    mesh.firstTriangleIndex = firstTriangleIndex;
    mesh.triangleCount = (int)scene.triangles.size() - firstTriangleIndex;
    mesh.materialIndex = materialIndex;
    mesh.boundsMin = boundsMin;
    mesh.boundsMax = boundsMax;
    scene.meshes.push_back(mesh);
}
