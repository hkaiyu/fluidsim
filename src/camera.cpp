#include "camera.hpp"

#include <GLFW/glfw3.h>

#define _USE_MATH_DEFINES
#include <math.h>

glm::mat4 ArcballCamera::getViewMatrix() const { return view; };
glm::mat4 ArcballCamera::getProjMatrix() const { return proj; };

glm::vec3 ArcballCamera::getViewDir() const { return glm::transpose(view)[2]; };
glm::vec3 ArcballCamera::getRightVec() const { return glm::transpose(view)[0]; };
glm::vec3 ArcballCamera::getCameraPos() const { return cameraPos; };

void ArcballCamera::setCameraView(glm::vec3 cameraPos, glm::vec3 lookAt, glm::vec3 up)
{
    this->cameraPos = cameraPos;
    this->lookAt = lookAt;
    this->up = up;

    updateView();
}

void ArcballCamera::setPerspectiveProjection(float fov, float aspect, float nearp, float farp)
{
    this->fov = fov;
    this->aspect = aspect;
    this->_near = nearp;
    this->_far = farp;

    updatePerspective();
}

void ArcballCamera::updateFov(float fov) { this->fov = fov; updatePerspective(); };
void ArcballCamera::updateAspect(float aspect) { this->aspect = aspect; updatePerspective(); };
void ArcballCamera::updateNearPlane(float nearp) { this->_near = nearp; updatePerspective(); };
void ArcballCamera::updateFarPlane(float farp) { this->_far = farp; updatePerspective(); };

void ArcballCamera::processGLFWScrollEvent(float, float yoffset)
{
    cameraPos = cameraPos + glm::normalize(lookAt - cameraPos) * yoffset * scrollSensitivity;
    updateView();
}

void ArcballCamera::processGLFWMouseButtonEvent(int button, int action, int mods, float xpos, float ypos)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            mouseIsDragging = true;
            lastMouseX = xpos;
            lastMouseY = ypos;
        }
        if (action == GLFW_RELEASE)
        {
            mouseIsDragging = false;
        }
    }
}

void ArcballCamera::processGLFWCursorMoveEvent(float xpos, float ypos, float vpwidth, float vpheight)
{
    if (mouseIsDragging)
    {
        glm::vec4 camPos(cameraPos, 1.0f);
        glm::vec4 pivot(lookAt, 1.0f);

        float dAngleX = 2 * M_PI / vpwidth;
        float dAngleY = M_PI / vpheight;
        float xangle = (xpos - lastMouseX) * dAngleX;
        float yangle = (ypos - lastMouseY) * dAngleY;

        glm::mat4 rotationX(1.0f);
        rotationX = glm::rotate(rotationX, xangle, up);
        camPos = (rotationX * (camPos - pivot)) + pivot;

        glm::mat4 rotationY(1.0f);
        rotationY = glm::rotate(rotationY, yangle, getRightVec());
        camPos = (rotationY * (camPos - pivot)) + pivot;

        setCameraView(camPos, lookAt, up);

        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

void ArcballCamera::updateView() { view = glm::lookAtLH(cameraPos, lookAt, up); };
void ArcballCamera::updatePerspective() { proj = glm::perspectiveLH_ZO(fov, aspect, _near, _far); };
