#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "sphere.h"
#include <GL/glew.h>
#include <vector>
#include <glm/glm.hpp>
#include <cmath>

SphereMesh createSphere(float radius, int sectorCount, int stackCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    float sectorStep = 2.0f * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;
    float lengthInv = 1.0f / radius;

    for (int i = 0; i <= stackCount; ++i) {
        float stackAngle = M_PI / 2.0f - i * stackStep; // pi/2 到 -pi/2
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * sectorStep;
            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            // 法线
            vertices.push_back(x * lengthInv);
            vertices.push_back(y * lengthInv);
            vertices.push_back(z * lengthInv);
            // 纹理坐标
            vertices.push_back((float)j / sectorCount);
            vertices.push_back((float)i / stackCount);
        }
    }

    for (int i = 0; i < stackCount; ++i) {
        int k1 = i * (sectorCount + 1);
        int k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i == 0) {
                // 北极：连接极点到第一环（所有 k1 点都在北极同一位置）
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            } else {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // 顶点属性：位置
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 法线
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 纹理坐标
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return {VAO, VBO, EBO, (int)indices.size()};
}

void deleteSphere(SphereMesh& mesh) {
    glDeleteVertexArrays(1, &mesh.VAO);
    glDeleteBuffers(1, &mesh.VBO);
    glDeleteBuffers(1, &mesh.EBO);
}

// ========== LOD 系统 ==========

SphereLOD createSphereLOD() {
    SphereLOD lod;
    // LOD 0: 近距高精度 — 128×64
    lod.levels[0] = createSphere(1.0f, 128, 64);
    // LOD 1: 中距标准 — 64×32
    lod.levels[1] = createSphere(1.0f, 64, 32);
    // LOD 2: 远距低精度 — 32×16
    lod.levels[2] = createSphere(1.0f, 32, 16);
    return lod;
}

LODSelection selectLOD(const SphereLOD& lod, float distToPlanet, float planetRadius, float fovYDeg, int screenHeight) {
    // 行星在屏幕上的角半径 (弧度)
    float angRadius = atan(planetRadius / distToPlanet);
    // 垂直FOV下每像素对应的弧度
    float fovYRad = fovYDeg * M_PI / 180.0f;
    float radPerPixel = fovYRad / screenHeight;
    // 行星在屏幕上的像素半径
    float pixelRadius = angRadius / radPerPixel;

    // 阈值：屏幕像素半径 > 150 → LOD0, > 50 → LOD1, else → LOD2
    if (pixelRadius > 150.0f)
        return { lod.levels[0].VAO, lod.levels[0].indexCount };
    else if (pixelRadius > 50.0f)
        return { lod.levels[1].VAO, lod.levels[1].indexCount };
    else
        return { lod.levels[2].VAO, lod.levels[2].indexCount };
}

void deleteSphereLOD(SphereLOD& lod) {
    for (int i = 0; i < LOD_COUNT; i++)
        deleteSphere(lod.levels[i]);
}
