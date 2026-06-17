#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>

glm::vec3 Config::parseVec3(const std::string& s) {
    glm::vec3 v(0.0f);
    std::stringstream ss(s);
    std::string token;
    int i = 0;
    while (std::getline(ss, token, ',') && i < 3) {
        size_t b = token.find_first_not_of(" \t");
        size_t e = token.find_last_not_of(" \t");
        if (b != std::string::npos)
            token = token.substr(b, e - b + 1);
        v[i++] = std::stof(token);
    }
    return v;
}

bool Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open config file: " << filename << std::endl;
        return false;
    }
    std::cout << "Loading config: " << filename << std::endl;

    std::string line, section;
    int planetCount = 0;
    std::vector<PlanetConfig> planetsTemp;
    std::vector<MoonConfig> moonsTemp;

    while (std::getline(file, line)) {
        // 去除注释
        auto comment = line.find('#');
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty()) continue;

        // 检查 section
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            // 去除 section 内部空格
            size_t b = section.find_first_not_of(" \t");
            size_t e = section.find_last_not_of(" \t");
            if (b != std::string::npos)
                section = section.substr(b, e - b + 1);
            std::cout << "Section: [" << section << "]" << std::endl;
            continue;
        }

        // 键值对
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) s = "";
            else s = s.substr(b, e - b + 1);
        };
        trim(key); trim(value);

        // 去掉键的前缀 "section."（如 camera.pitch → pitch）
        {
            std::string prefix = section + ".";
            if (key.find(prefix) == 0) {
                key = key.substr(prefix.size());
                trim(key);
            }
        }

        std::cout << "  [" << section << "] " << key << " = " << value << std::endl;

        if (section == "window") {
            if (key == "width") windowWidth = std::stoi(value);
            else if (key == "height") windowHeight = std::stoi(value);
            else if (key == "fullscreen") fullscreen = (std::stoi(value) != 0);
        } else if (section == "camera") {
            if (key == "posX") cameraPos.x = std::stod(value);
            else if (key == "posY") cameraPos.y = std::stod(value);
            else if (key == "posZ") cameraPos.z = std::stod(value);
            else if (key == "yaw") cameraYaw = std::stof(value);
            else if (key == "pitch") cameraPitch = std::stof(value);
            else if (key == "roll") cameraRoll = std::stof(value);
            else if (key == "fov") cameraFov = std::stof(value);
            else if (key == "speed") cameraSpeed = std::stof(value);
            else if (key == "sensitivity") cameraSensitivity = std::stof(value);
        } else if (section == "simulation") {
            if (key == "timeScale") timeScale = std::stof(value);
        } else if (section == "settings") {
            if (key == "bloom") bloomEnabled = (std::stoi(value) != 0);
            else if (key == "flare") flareEnabled = (std::stoi(value) != 0);
            else if (key == "autoExposure") autoExposure = (std::stoi(value) != 0);
            else if (key == "wireframe") wireframe = (std::stoi(value) != 0);
            else if (key == "manualExposure") manualExposure = std::stof(value);
            else if (key == "resolution") { int v=std::stoi(value); resolutionIndex = v<0 ? 0 : (v>13 ? 13 : v); }
            else if (key == "fullscreen") fullscreen = (std::stoi(value) != 0);
            else if (key == "shaderPath") shaderPath = value;
        } else if (section == "planets") {
            if (key == "count") planetCount = std::stoi(value);
        } else if (section.find("planet.") == 0) {
            std::string name = section.substr(7); // 跳过 "planet."
            PlanetConfig* pc = nullptr;
            for (auto& p : planetsTemp) {
                if (p.name == name) { pc = &p; break; }
            }
            if (!pc) {
                PlanetConfig newPlanet;
                newPlanet.name = name;
                planetsTemp.push_back(newPlanet);
                pc = &planetsTemp.back();
                std::cout << "  -> Added planet: " << name << std::endl;
            }
            if (key == "texture") pc->texturePath = value;
            else if (key == "color") pc->color = parseVec3(value);
            else if (key == "orbitRadius") pc->orbitRadius = std::stof(value);
            else if (key == "orbitPeriod") pc->orbitPeriod = std::stof(value);
            else if (key == "rotationPeriod") pc->rotationPeriod = std::stof(value);
            else if (key == "size") pc->size = std::stof(value);
            else if (key == "roughness") pc->roughness = std::stof(value);
            else if (key == "metallic") pc->metallic = std::stof(value);
            else if (key == "inclination") pc->inclination = std::stof(value);
            else if (key == "eccentricity") pc->eccentricity = std::stof(value);
            else if (key == "longitudeAscendingNode") pc->longitudeAscendingNode = std::stof(value);
            else if (key == "argumentOfPeriapsis") pc->argumentOfPeriapsis = std::stof(value);
            else if (key == "meanAnomalyAtEpoch") pc->meanAnomalyAtEpoch = std::stof(value);
            else if (key == "axialTilt") pc->axialTilt = std::stof(value);
        } else if (section.find("moon.") == 0 && section != "moons") {
            // [moon.earth_moon] → name = "earth_moon"
            std::string name = section.substr(5);
            MoonConfig* mc = nullptr;
            for (auto& m : moonsTemp) {
                if (m.name == name) { mc = &m; break; }
            }
            if (!mc) {
                MoonConfig newMoon;
                newMoon.name = name;
                moonsTemp.push_back(newMoon);
                mc = &moonsTemp.back();
            }
            if (key == "parent") mc->parent = value;
            else if (key == "texture") mc->texturePath = value;
            else if (key == "color") mc->color = parseVec3(value);
            else if (key == "orbitRadius") mc->orbitRadius = std::stof(value);
            else if (key == "orbitPeriod") mc->orbitPeriod = std::stof(value);
            else if (key == "meanAnomalyAtEpoch") mc->meanAnomalyAtEpoch = std::stof(value);
            else if (key == "size") mc->size = std::stof(value);
        }
    }

    std::cout << "Planet count from config: " << planetCount << ", parsed: " << planetsTemp.size() << std::endl;
    std::cout << "Moon count parsed: " << moonsTemp.size() << std::endl;
    if (planetCount != (int)planetsTemp.size()) {
        std::cerr << "Warning: planets.count mismatch" << std::endl;
    }
    planets = std::move(planetsTemp);
    moons = std::move(moonsTemp);

    // 应用分辨率索引 → windowWidth/Height（与 menu.cpp RESOLUTIONS 对齐）
    static const int RES[][2] = {
        { 800,  600}, {1024,  768}, {1280,  960}, {1600, 1200},
        {1280,  800}, {1440,  900}, {1920, 1200}, {2560, 1600},
        {1280,  720}, {1366,  768}, {1600,  900}, {1920, 1080},
        {2560, 1440}, {3840, 2160}
    };
    windowWidth  = RES[resolutionIndex][0];
    windowHeight = RES[resolutionIndex][1];

    return true;
}

// 判断行是否为 section 头
static bool isSectionHeader(const std::string& line, std::string& sectionName) {
    if (line.empty() || line[0] != '[') return false;
    size_t end = line.find(']');
    if (end == std::string::npos) return false;
    sectionName = line.substr(1, end - 1);
    // 去空格
    size_t b = sectionName.find_first_not_of(" \t");
    size_t e = sectionName.find_last_not_of(" \t");
    if (b != std::string::npos) sectionName = sectionName.substr(b, e - b + 1);
    return true;
}

// 静态段：不随时间变化的配置，save() 保留其内容
static bool isStaticSection(const std::string& name) {
    if (name == "planets")      return true;
    if (name.find("planet.") == 0) return true;
    if (name == "moons")        return true;
    if (name.find("moon.") == 0)  return true;
    return false;
}

bool Config::save(const std::string& filename) const {
    // 读取现有文件，保留静态段（planets / planet.* / moons / moon.*）
    std::vector<std::string> staticLines;
    {
        std::ifstream inFile(filename);
        if (inFile.is_open()) {
            std::string line, curSection;
            bool inStatic = false;
            while (std::getline(inFile, line)) {
                std::string trimmed = line;
                size_t s = trimmed.find_first_not_of(" \t\r\n");
                size_t e = trimmed.find_last_not_of(" \t\r\n");
                if (s != std::string::npos) trimmed = trimmed.substr(s, e - s + 1);
                // 检测 section
                std::string sec;
                if (isSectionHeader(line, sec)) {
                    curSection = sec;
                    inStatic = isStaticSection(sec);
                }
                if (inStatic) {
                    staticLines.push_back(line);
                }
            }
        }
    }

    // 写完整文件
    std::ofstream outFile(filename, std::ios::trunc);
    if (!outFile.is_open()) {
        std::cerr << "ERROR: Cannot write config: " << filename << std::endl;
        return false;
    }

    // --- [window] ---
    outFile << "[window]\n";
    outFile << "window.width       = " << windowWidth << "\n";
    outFile << "window.height      = " << windowHeight << "\n";
    outFile << "window.fullscreen  = " << (fullscreen ? 1 : 0) << "\n";
    outFile << "\n";

    // --- [camera] ---
    // 注意：相机位置/朝向不在关闭时保存，每次启动使用初始值
    outFile << "[camera]\n";
    outFile << "camera.posX        = 0\n";
    outFile << "camera.posY        = 3000\n";
    outFile << "camera.posZ        = 10000\n";
    outFile << "camera.yaw         = -90.0\n";
    outFile << "camera.pitch       = -16.699\n";
    outFile << "camera.roll        = 0.0\n";
    outFile << "camera.fov         = " << cameraFov << "\n";
    outFile << "camera.speed       = " << cameraSpeed << "\n";
    outFile << "camera.sensitivity = " << cameraSensitivity << "\n";
    outFile << "\n";

    // --- [simulation] ---
    outFile << "[simulation]\n";
    outFile << "simulation.timeScale = " << timeScale << "  ; 真实时间，行星按实际天文学周期运行\n";
    outFile << "\n";

    // --- 静态段（planets / planet.* / moons / moon.*）---
    if (!staticLines.empty()) {
        for (const auto& l : staticLines) outFile << l << "\n";
    } else {
        // 从向量重新生成行星/卫星数据
        outFile << "\n[planets]\n";
        outFile << "count              = " << planets.size() << "\n\n";
        for (const auto& p : planets) {
            outFile << "[planet." << p.name << "]\n";
            outFile << "planet." << p.name << ".texture      = " << p.texturePath << "\n";
            outFile << "planet." << p.name << ".color        = "
                      << p.color.x << ", " << p.color.y << ", " << p.color.z << "\n";
            outFile << "planet." << p.name << ".orbitRadius  = " << p.orbitRadius << "\n";
            outFile << "planet." << p.name << ".orbitPeriod  = " << p.orbitPeriod << "\n";
            outFile << "planet." << p.name << ".rotationPeriod = " << p.rotationPeriod << "\n";
            outFile << "planet." << p.name << ".size         = " << p.size << "\n";
            outFile << "planet." << p.name << ".roughness    = " << p.roughness << "\n";
            outFile << "planet." << p.name << ".metallic     = " << p.metallic << "\n";
            outFile << "planet." << p.name << ".inclination  = " << p.inclination << "\n";
            outFile << "planet." << p.name << ".eccentricity = " << p.eccentricity << "\n";
            outFile << "planet." << p.name << ".longitudeAscendingNode = " << p.longitudeAscendingNode << "\n";
            outFile << "planet." << p.name << ".argumentOfPeriapsis    = " << p.argumentOfPeriapsis << "\n";
            outFile << "planet." << p.name << ".meanAnomalyAtEpoch     = " << p.meanAnomalyAtEpoch << "\n";
            outFile << "planet." << p.name << ".axialTilt              = " << p.axialTilt << "\n\n";
        }
        if (!moons.empty()) {
            outFile << "[moons]\n";
            outFile << "count = " << moons.size() << "\n\n";
            for (const auto& m : moons) {
                outFile << "[moon." << m.name << "]\n";
                outFile << "moon." << m.name << ".parent    = " << m.parent << "\n";
                outFile << "moon." << m.name << ".texture   = " << m.texturePath << "\n";
                outFile << "moon." << m.name << ".color     = "
                          << m.color.x << ", " << m.color.y << ", " << m.color.z << "\n";
                outFile << "moon." << m.name << ".orbitRadius = " << m.orbitRadius << "\n";
                outFile << "moon." << m.name << ".orbitPeriod = " << m.orbitPeriod << "\n";
                outFile << "moon." << m.name << ".meanAnomalyAtEpoch = " << m.meanAnomalyAtEpoch << "\n";
                outFile << "moon." << m.name << ".size      = " << m.size << "\n\n";
            }
        }
    }

    // --- [settings] ---
    outFile << "\n[settings]\n";
    outFile << "bloom          = " << (bloomEnabled ? 1 : 0) << "\n";
    outFile << "flare          = " << (flareEnabled ? 1 : 0) << "\n";
    outFile << "autoExposure   = " << (autoExposure ? 1 : 0) << "\n";
    outFile << "wireframe      = " << (wireframe ? 1 : 0) << "\n";
    outFile << "manualExposure = " << manualExposure << "\n";
    outFile << "resolution    = " << resolutionIndex << "\n";
    outFile << "fullscreen     = " << (fullscreen ? 1 : 0) << "\n";
    outFile << "shaderPath    = " << shaderPath << "\n";

    std::cout << "Config saved to " << filename << std::endl;
    return true;
}
