#pragma once
#include <glm/glm.hpp>

struct SphereMesh {
    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
    int indexCount;
};

SphereMesh createSphere(float radius, int sectorCount, int stackCount);
void deleteSphere(SphereMesh& mesh);

// ========== LOD 系统 ==========

constexpr int LOD_COUNT = 4;

struct SphereLOD {
    SphereMesh levels[LOD_COUNT];
};

// 创建3级LOD球体 (高/中/低精度)
SphereLOD createSphereLOD();

// 根据相机到行星距离选择LOD级别，返回 {VAO, indexCount}
// distToPlanet: 世界空间距离, planetRadius: 行星世界空间半径, fovYDeg: 垂直FOV角度
struct LODSelection { unsigned int VAO; int indexCount; };
LODSelection selectLOD(const SphereLOD& lod, float distToPlanet, float planetRadius, float fovYDeg, int screenHeight);

void deleteSphereLOD(SphereLOD& lod);
