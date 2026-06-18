#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct CollisionSphere {
    glm::dvec3 center;
    double radius;
};

class Camera {
public:
    Camera(glm::dvec3 pos, float yaw, float pitch, float roll, float fov, float speed, float sensitivity);

    void processKeyboard(int key, int action, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset);
    void processMouseScroll(float yoffset);
    void update(float deltaTime);
    void setCollisionBoundaries(const std::vector<CollisionSphere>& spheres, double boundaryFactor = 1.002);
    void clampToBoundary(const std::vector<CollisionSphere>& spheres, double marginFactor = 1.002);
    glm::mat4 getViewMatrix() const;
    float getFov() const { return fov; }
    glm::dvec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }
    float getYaw() const { return glm::degrees(glm::yaw(orientation)); }
    float getPitch() const { return glm::degrees(glm::pitch(orientation)); }
    float getRoll() const { return glm::degrees(glm::roll(orientation)); }
    float getSpeed() const { return movementSpeed; }
    float getSensitivity() const { return mouseSensitivity; }

private:
    void updateVectors();
    glm::dvec3 filterDisplacement(const glm::dvec3& disp) const;
    glm::dvec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    glm::quat orientation;
    float fov;
    float movementSpeed;
    float mouseSensitivity;
    float currentSpeed;

    bool keyW = false, keyA = false, keyS = false, keyD = false;
    bool keyQ = false, keyE = false, keyR = false, keyF = false;

    std::vector<CollisionSphere> collisionSpheres;
    double boundaryFactor = 1.002;
};
