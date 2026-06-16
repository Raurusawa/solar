#pragma once
#include <string>
#include <glm/glm.hpp>
#include <vector>

struct PlanetConfig {
    std::string name;
    std::string texturePath;
    glm::vec3 color;
    float orbitRadius;
    float orbitPeriod;   // 地球日
    float rotationPeriod;// 地球日
    float size;
    float roughness = 0.7f;
    float metallic  = 0.0f;
};

class Config {
public:
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;

    glm::vec3 cameraPos{0.0f, 30.0f, 120.0f};
    float cameraYaw = -90.0f;
    float cameraPitch = -20.0f;
    float cameraRoll = 0.0f;
    float cameraFov = 45.0f;
    float cameraSpeed = 20.0f;
    float cameraSensitivity = 0.1f;

    float timeScale = 86400.0f; // 模拟加速

    std::vector<PlanetConfig> planets;

    bool load(const std::string& filename);

private:
    static glm::vec3 parseVec3(const std::string& s);
};
