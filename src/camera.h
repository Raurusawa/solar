#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

class Camera {
public:
    Camera(glm::vec3 pos, float yaw, float pitch, float roll, float fov, float speed, float sensitivity);

    void processKeyboard(int key, int action, float deltaTime);
    void processMouseMovement(float xoffset, float yoffset);
    void processMouseScroll(float yoffset);
    void update(float deltaTime);
    glm::mat4 getViewMatrix() const;
    float getFov() const { return fov; }
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }

private:
    void updateVectors();
    glm::vec3 position;
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
