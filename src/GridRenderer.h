#pragma once
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "Camera.h"

class GridRenderer {
public:
    GridRenderer(WGPUDevice device);
    ~GridRenderer();

    void render(WGPURenderPassEncoder renderPass, const Camera& camera);
    void cleanup();

private:
    void createPipeline();
    void createVertexBuffer();
    void createUniformBuffer();
    void createBindGroup();
    void updateUniformBuffer(const Camera& camera);

    // Grid configuration
    static constexpr float GRID_SIZE = 20.0f;  // Total size of the grid
    static constexpr float GRID_SPACING = 1.0f; // Space between grid lines
    static constexpr float MAJOR_LINE_INTERVAL = 5.0f; // Interval for major (darker) lines

    // Structure for vertices
    struct Vertex {
        float position[3];  // x, y, z coordinates
        float color[4];     // rgba color with alpha for different line weights
    };

    // Structure for uniform buffer
    struct UniformData {
        glm::mat4 viewProj;
    };

    std::vector<Vertex> generateGridVertices();

    WGPUDevice device;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    std::vector<Vertex> vertices;
    UniformData uniformData;
};