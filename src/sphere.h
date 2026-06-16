#pragma once

struct SphereMesh {
    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
    int indexCount;
};

SphereMesh createSphere(float radius, int sectorCount, int stackCount);
void deleteSphere(SphereMesh& mesh);
