#pragma once
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"

class TriangleRenderer {
public:
    TriangleRenderer(WGPUDevice device);
    ~TriangleRenderer();

    void render(WGPURenderPassEncoder renderPass, const Camera& camera);
    void update(float deltaTime);  // New update function for rotation
    void cleanup();

private:
    void createPipeline();
    void createVertexBuffer();
    void createUniformBuffer();
    void createBindGroup();
    void updateUniformBuffer(const Camera& camera);

    WGPUDevice device;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    // Basic vertex data for a triangle
    struct Vertex {
        float position[3];  // x, y, z coordinates (now 3D!)
        float color[3];     // rgb color
    };

    static constexpr Vertex vertices[3] = {
        { {  0.0f,  -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f} }, // Top (red)
        { { -0.5f,   0.5f, 0.0f}, {0.0f, 1.0f, 0.0f} }, // Bottom left (green)
        { {  0.5f,   0.5f, 0.0f}, {0.0f, 0.0f, 1.0f} }  // Bottom right (blue)
    };

    // Uniform buffer data
    struct UniformData {
        glm::mat4 modelViewProj;
    };

    float rotationAngle = 0.0f;
    UniformData uniformData;
};