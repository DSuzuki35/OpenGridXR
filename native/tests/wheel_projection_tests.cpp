#include "wheel_projection.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        unsigned samples = 0;
        for (float eyeX : {-.08f, -.032f, .032f, .11f})
            for (float eyeY : {-.07f, 0.f, .09f})
                for (float eyeZ : {.25f, .45f, 1.2f}) {
                    const XrVector3f eye{eyeX, eyeY, eyeZ};
                    const float aperture[]{.025f, -.015f, .42f, .28f};
                    const XrPosef camera{{0, .25881905f, 0, .96592583f}, {2, 3, 4}};
                    XrSwapchainSubImage image{};
                    image.imageRect = {{0, 0}, {640, 640}};
                    const auto view = og::wheel_projection_view(camera, eye, aperture, image);
                    check(view.type == XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW && !view.next,
                          "valid projection view header");
                    check(view.subImage.imageRect.extent.width == 640 &&
                          view.pose.orientation.y == camera.orientation.y &&
                          view.pose.position.z == camera.position.z,
                          "preserve rotated camera pose and subimage");
                    const float left = std::tan(view.fov.angleLeft), right = std::tan(view.fov.angleRight);
                    const float up = std::tan(view.fov.angleUp), down = std::tan(view.fov.angleDown);
                    check(left < right && down < up, "ordered asymmetric FOV");
                    for (float x : {-.15f, 0.f, .15f})
                        for (float y : {-.08f, .08f})
                            for (float z : {-.06f, 0.f, .06f}) {
                                const float w = eyeZ - z;
                                const float shaderX = 2 * ((eyeZ*x-eyeX*z)/w-aperture[0])/aperture[2];
                                const float shaderY = 2 * ((eyeZ*y-eyeY*z)/w-aperture[1])/aperture[3];
                                const float projectedX = (2*(x-eyeX)/w-right-left)/(right-left);
                                const float projectedY = (2*(y-eyeY)/w-up-down)/(up-down);
                                check(std::abs(shaderX-projectedX) < .00002f &&
                                      std::abs(shaderY-projectedY) < .00002f,
                                      "projection matches existing mesh shader across model depths");
                                ++samples;
                            }
                }
        std::cout << samples << " wheel projection samples passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
