#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

class Camera {
public:
    Camera(glm::dvec3 pos, float yaw, float pitch, float roll, float fov, float speed, float sensitivity);

    void processKeyboard(int key, int action, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset);
    void processMouseScroll(float yoffset);
    void update(float deltaTime);
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
    glm::dvec3 position;       // 双精度位置，避免大坐标下精度丢失
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
};
