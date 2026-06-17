#pragma once
#include <string>
#include <glm/glm.hpp>
#include <vector>

struct PlanetConfig {
    std::string name;
    std::string texturePath;
    glm::vec3 color;
    float orbitRadius;             // 半长轴 (system units, 地球=1000)
    float orbitPeriod;             // 公转周期 (地球日)
    float rotationPeriod;          // 自转周期 (地球日, 负=逆行)
    float size;
    float roughness = 0.7f;
    float metallic  = 0.0f;
    // 轨道力学参数 (J2000.0 黄道面)
    float inclination = 0.0f;           // 轨道倾角 (度)
    float eccentricity = 0.0f;          // 轨道偏心率
    float longitudeAscendingNode = 0.0f;// 升交点黄经 (度)
    float argumentOfPeriapsis = 0.0f;   // 近日点幅角 (度)
    float meanAnomalyAtEpoch = 0.0f;    // J2000.0 历元平近点角 (度)
    float axialTilt = 0.0f;             // 自转轴倾角 (度, 相对轨道面法线)
};

struct MoonConfig {
    std::string name;
    std::string parent;         // 母行星名字
    std::string texturePath;    // 纹理路径
    glm::vec3 color;            // fallback颜色
    float orbitRadius;          // 绕行星轨道半长轴 (system units)
    float orbitPeriod;          // 公转周期 (地球日)
    float meanAnomalyAtEpoch;   // J2000.0 平近点角 (度)
    float size;                 // 半径 (system units)
};

class Config {
public:
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;

    glm::dvec3 cameraPos{0.0, 30.0, 120.0};
    float cameraYaw = -90.0f;
    float cameraPitch = -20.0f;
    float cameraRoll = 0.0f;
    float cameraFov = 45.0f;
    float cameraSpeed = 20.0f;
    float cameraSensitivity = 0.1f;

    float timeScale = 86400.0f; // 模拟加速

    // 分辨率索引：0-3=4:3, 4-7=16:10, 8-13=16:9（与 menu.cpp RESOLUTIONS 对齐）
    int resolutionIndex = 0;

    std::vector<PlanetConfig> planets;
    std::vector<MoonConfig> moons;

    // ---- 可保存的设置（菜单中修改的） ----
    bool bloomEnabled = true;
    bool flareEnabled = false;
    bool autoExposure = false;
    bool wireframe = false;
    float manualExposure = 0.0f;

    // shader 文件所在目录（相对 exe 或绝对路径，需以 / 结尾）
    std::string shaderPath = "src/shaders/";

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;

private:
    static glm::vec3 parseVec3(const std::string& s);
};
