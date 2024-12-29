#include "GridRenderer.h"
#include <cstring>

GridRenderer::GridRenderer(WGPUDevice device) : device(device) {
    vertices = generateGridVertices();
    createUniformBuffer();
    createBindGroup();
    createPipeline();
    createVertexBuffer();
}

GridRenderer::~GridRenderer() {
    cleanup();
}

void GridRenderer::cleanup() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
}

std::vector<GridRenderer::Vertex> GridRenderer::generateGridVertices() {
    std::vector<Vertex> gridVertices;
    
    // Calculate number of lines in each direction
    int numLines = static_cast<int>(GRID_SIZE / GRID_SPACING) * 2 + 1;
    float halfSize = GRID_SIZE;
    
    // Generate horizontal and vertical lines
    for (int i = 0; i < numLines; i++) {
        float pos = -halfSize + i * GRID_SPACING;
        bool isMajor = (static_cast<int>(std::abs(pos)) % static_cast<int>(MAJOR_LINE_INTERVAL)) == 0;
        float alpha = isMajor ? 0.5f : 0.25f;
        
        // Horizontal lines
        gridVertices.push_back({{-halfSize, 0.0f, pos}, {1.0f, 1.0f, 1.0f, alpha}});
        gridVertices.push_back({{halfSize, 0.0f, pos}, {1.0f, 1.0f, 1.0f, alpha}});
        
        // Vertical lines
        gridVertices.push_back({{pos, 0.0f, -halfSize}, {1.0f, 1.0f, 1.0f, alpha}});
        gridVertices.push_back({{pos, 0.0f, halfSize}, {1.0f, 1.0f, 1.0f, alpha}});
    }

    // Add X axis (red)
    gridVertices.push_back({{-halfSize, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}});
    gridVertices.push_back({{halfSize, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}});

    // Add Z axis (blue)
    gridVertices.push_back({{0.0f, 0.0f, -halfSize}, {1.0f, 1.0f, 1.0f, 1.0f}});
    gridVertices.push_back({{0.0f, 0.0f, halfSize}, {1.0f, 1.0f, 1.0f, 1.0f}});

    return gridVertices;
}

void GridRenderer::createUniformBuffer() {
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(UniformData);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
}

void GridRenderer::createBindGroup() {
    // Create bind group layout
    WGPUBindGroupLayoutEntry bglEntry = {};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.minBindingSize = sizeof(UniformData);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Create bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(UniformData);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
}

void GridRenderer::createPipeline() {
    // Shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
        struct Uniforms {
            viewProj: mat4x4<f32>,
        }
        @binding(0) @group(0) var<uniform> uniforms: Uniforms;

        struct VertexInput {
            @location(0) position: vec3f,
            @location(1) color: vec4f,
        };

        struct VertexOutput {
            @builtin(position) position: vec4f,
            @location(0) color: vec4f,
        };

        @vertex
        fn vs_main(in: VertexInput) -> VertexOutput {
            var out: VertexOutput;
            out.position = uniforms.viewProj * vec4f(in.position, 1.0);
            out.color = in.color;
            return out;
        }

        @fragment
        fn fs_main(@location(0) color: vec4f) -> @location(0) vec4f {
            return color;
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Vertex state
    WGPUVertexAttribute attributes[2] = {};
    // Position attribute
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[0].shaderLocation = 0;
    // Color attribute
    attributes[1].format = WGPUVertexFormat_Float32x4;
    attributes[1].offset = offsetof(Vertex, color);
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Vertex);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 2;
    vertexBufferLayout.attributes = attributes;

    // Pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Fragment state with alpha blending
    WGPUBlendState blend = {};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha = blend.color;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    multisample.alphaToCoverageEnabled = false;
    pipelineDesc.multisample = multisample;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void GridRenderer::createVertexBuffer() {
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = vertices.size() * sizeof(Vertex);
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bufferDesc.mappedAtCreation = true;

    vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    void* data = wgpuBufferGetMappedRange(vertexBuffer, 0, bufferDesc.size);
    memcpy(data, vertices.data(), bufferDesc.size);
    wgpuBufferUnmap(vertexBuffer);
}

void GridRenderer::updateUniformBuffer(const Camera& camera) {
    uniformData.viewProj = camera.getProjection() * camera.getView();

    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &uniformData,
        sizeof(UniformData)
    );
}

void GridRenderer::render(WGPURenderPassEncoder renderPass, const Camera& camera) {
    updateUniformBuffer(camera);
    
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, vertices.size() * sizeof(Vertex));
    wgpuRenderPassEncoderDraw(renderPass, vertices.size(), 1, 0, 0);
}