#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "sphere.h"
#include <GL/glew.h>
#include <vector>
#include <glm/glm.hpp>
#include <cmath>
#include <iostream>

SphereMesh createSphere(float radius, int sectorCount, int stackCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    float sectorStep = 2.0f * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;
    float lengthInv = 1.0f / radius;

    for (int i = 0; i <= stackCount; ++i) {
        float stackAngle = M_PI / 2.0f - i * stackStep; // pi/2(北极) 到 -pi/2(南极)
        float xz = radius * cosf(stackAngle);  // 赤道半径
        float y = radius * sinf(stackAngle);   // Y轴=极轴, Y+=北极

        for (int j = 0; j <= sectorCount; ++j) {
            float sectorAngle = j * sectorStep;
            float x = xz * cosf(sectorAngle);
            float z = xz * sinf(sectorAngle);
            vertices.push_back(x);
            vertices.push_back(y);   // Y=极轴 (纹理北极在Y+)
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
    // LOD 0: 极近距超高精度 — 384×192
    lod.levels[0] = createSphere(1.0f, 384, 192);
    // LOD 1: 近距高精度 — 192×96
    lod.levels[1] = createSphere(1.0f, 192, 96);
    // LOD 2: 中距标准 — 96×48
    lod.levels[2] = createSphere(1.0f, 96, 48);
    // LOD 3: 远距低精度 — 48×24
    lod.levels[3] = createSphere(1.0f, 48, 24);
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

    // 相机与行星表面的距离（以行星半径为单位）
    float distInRadii = distToPlanet / planetRadius;

    // 双重策略：屏幕空间 + 绝对近距离保护
    int lodLevel = 3;
    if (pixelRadius > 60.0f || distInRadii < 3.0f)       lodLevel = 0;
    else if (pixelRadius > 20.0f || distInRadii < 10.0f)   lodLevel = 1;
    else if (pixelRadius > 8.0f || distInRadii < 30.0f)    lodLevel = 2;

    // 诊断：对极远距离或小卫星每60帧打印一次LOD
    static int frameCounter = 0;
    frameCounter++;
    if (pixelRadius > 10.0f && frameCounter % 60 == 0) {
        std::cout << "[LOD] pixR=" << pixelRadius << " distR=" << distInRadii
                  << " -> LOD" << lodLevel << " iCount=" << lod.levels[lodLevel].indexCount << std::endl;
    }

    return { lod.levels[lodLevel].VAO, lod.levels[lodLevel].indexCount };
}

void deleteSphereLOD(SphereLOD& lod) {
    for (int i = 0; i < LOD_COUNT; i++)
        deleteSphere(lod.levels[i]);
}
