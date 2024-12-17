#include "TriangleRenderer.h"
#include <cstring>

TriangleRenderer::TriangleRenderer(WGPUDevice device) : device(device) {
    createUniformBuffer();
    createBindGroup();
    createPipeline();
    createVertexBuffer();
}

TriangleRenderer::~TriangleRenderer() {
    cleanup();
}

void TriangleRenderer::cleanup() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
}

void TriangleRenderer::update(float deltaTime) {
    rotationAngle += deltaTime; // Rotate 1 radian per second
}

void TriangleRenderer::createUniformBuffer() {
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(UniformData);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
}

void TriangleRenderer::updateUniformBuffer(const Camera& camera) {
    // Create transformation matrices
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    
    // WebGPU's coordinate system has a different handedness than OpenGL
    proj[1][1] *= -1;

    uniformData.modelViewProj = camera.getProjection() * camera.getView() * model;

    // Update buffer
    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &uniformData,
        sizeof(UniformData)
    );
}

void TriangleRenderer::createBindGroup() {
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

void TriangleRenderer::createPipeline() {
    // Shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
        struct Uniforms {
            modelViewProj: mat4x4<f32>,
        }
        @binding(0) @group(0) var<uniform> uniforms: Uniforms;

        struct VertexInput {
            @location(0) position: vec3f,
            @location(1) color: vec3f,
        };

        struct VertexOutput {
            @builtin(position) position: vec4f,
            @location(0) color: vec3f,
        };

        @vertex
        fn vs_main(in: VertexInput) -> VertexOutput {
            var out: VertexOutput;
            out.position = uniforms.modelViewProj * vec4f(in.position, 1.0);
            out.color = in.color;
            return out;
        }

        @fragment
        fn fs_main(@location(0) color: vec3f) -> @location(0) vec4f {
            return vec4f(color, 1.0);
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Vertex state
    WGPUVertexAttribute attributes[2] = {};
    // Position attribute
    attributes[0].format = WGPUVertexFormat_Float32x3;  // Now using 3D positions
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    // Color attribute
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = 3 * sizeof(float);  // Offset adjusted for 3D position
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

    // Fragment state
    WGPUBlendState blend = {};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_Zero;
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

    // Multisample state
    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    multisample.alphaToCoverageEnabled = false;

    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample = multisample;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void TriangleRenderer::createVertexBuffer() {
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(vertices);
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bufferDesc.mappedAtCreation = true;

    vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    void* data = wgpuBufferGetMappedRange(vertexBuffer, 0, bufferDesc.size);
    memcpy(data, vertices, sizeof(vertices));
    wgpuBufferUnmap(vertexBuffer);
}

void TriangleRenderer::render(WGPURenderPassEncoder renderPass, const Camera& camera) {
    updateUniformBuffer(camera);
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, sizeof(vertices));
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
}