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
            if (key == "posX") cameraPos.x = std::stof(value);
            else if (key == "posY") cameraPos.y = std::stof(value);
            else if (key == "posZ") cameraPos.z = std::stof(value);
            else if (key == "yaw") cameraYaw = std::stof(value);
            else if (key == "pitch") cameraPitch = std::stof(value);
            else if (key == "roll") cameraRoll = std::stof(value);
            else if (key == "fov") cameraFov = std::stof(value);
            else if (key == "speed") cameraSpeed = std::stof(value);
            else if (key == "sensitivity") cameraSensitivity = std::stof(value);
        } else if (section == "simulation") {
            if (key == "timeScale") timeScale = std::stof(value);
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
        }
    }

    std::cout << "Planet count from config: " << planetCount << ", parsed: " << planetsTemp.size() << std::endl;
    if (planetCount != (int)planetsTemp.size()) {
        std::cerr << "Warning: planets.count mismatch" << std::endl;
    }
    planets = std::move(planetsTemp);
    return true;
}
