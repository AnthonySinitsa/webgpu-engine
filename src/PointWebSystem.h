#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Camera.h"

struct Point {
    float position[3];  // x, y, z position
};

struct UniformData {
    alignas(16) glm::mat4 viewProj;  // 64 bytes, aligned to 16
    alignas(16) float time;          // 4 bytes, aligned to 16
};

class PointWebSystem {
public:
    PointWebSystem(WGPUDevice device);
    ~PointWebSystem();

    void render(WGPURenderPassEncoder renderPass, const Camera& camera);
    void update(float deltaTime);

private:
    static constexpr int NUM_POINTS = 10;
    static constexpr float CIRCLE_RADIUS = 2.0f;
    static constexpr float ROTATION_SPEED = 0.05f;
    
    void createPipelineAndResources();
    void createBuffers();
    void createBindGroup();
    void initPoints();
    void updateUniforms(const Camera& camera);

    WGPUDevice device;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    std::vector<Point> points;
    UniformData uniformData;
    float currentTime = 0.0f;
};