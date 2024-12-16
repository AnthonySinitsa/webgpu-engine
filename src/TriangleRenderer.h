#pragma once
#include <webgpu/webgpu.h>

class TriangleRenderer {
public:
    TriangleRenderer(WGPUDevice device);
    ~TriangleRenderer();

    void render(WGPURenderPassEncoder renderPass);
    void cleanup();

private:
    void createPipeline();
    void createVertexBuffer();

    WGPUDevice device;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer vertexBuffer = nullptr;

    // Basic vertex data for a triangle
    struct Vertex {
        float position[2];  // x, y coordinates (2D for now)
        float color[3];     // rgb color
    };

    static constexpr Vertex vertices[3] = {
        { {  0.0f,   0.5f}, {1.0f, 0.0f, 0.0f} }, // Top (red)
        { { -0.5f,  -0.5f}, {0.0f, 1.0f, 0.0f} }, // Bottom left (green)
        { {  0.5f,  -0.5f}, {0.0f, 0.0f, 1.0f} }  // Bottom right (blue)
    };
};
