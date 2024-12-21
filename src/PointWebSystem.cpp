#include "PointWebSystem.h"
#include <cstring>
#include <iostream>

PointWebSystem::PointWebSystem(WGPUDevice device) : device(device) {
    initPoints();
    createBuffers();
    createPipelineAndResources();
    createComputePipeline();
}

PointWebSystem::~PointWebSystem() {
    if (vertexBufferA) wgpuBufferRelease(vertexBufferA);
    if (vertexBufferB) wgpuBufferRelease(vertexBufferB);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (renderPipeline) wgpuRenderPipelineRelease(renderPipeline);
    if (computePipeline) wgpuComputePipelineRelease(computePipeline);
    if (renderBindGroup) wgpuBindGroupRelease(renderBindGroup);
    if (computeBindGroupA) wgpuBindGroupRelease(computeBindGroupA);
    if (computeBindGroupB) wgpuBindGroupRelease(computeBindGroupB);
    if (renderBindGroupLayout) wgpuBindGroupLayoutRelease(renderBindGroupLayout);
    if (computeBindGroupLayout) wgpuBindGroupLayoutRelease(computeBindGroupLayout);
}

void PointWebSystem::initPoints() {
    points.resize(NUM_POINTS);
    
    // Initialize points in a straight line along the X axis
    for (int i = 0; i < NUM_POINTS; i++) {
        points[i].position[0] = (i - NUM_POINTS/2) * POINT_SPACING; // X position
        points[i].position[1] = -1.0f;                              // Y position
        points[i].position[2] = 0.0f;                               // Z position
    }
}

void PointWebSystem::createPipelineAndResources() {
    // Create render bind group layout first
    WGPUBindGroupLayoutEntry bglEntry = {};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.hasDynamicOffset = false;
    bglEntry.buffer.minBindingSize = sizeof(UniformData);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    
    renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    printf("Created render bind group layout: %p\n", (void*)renderBindGroupLayout);

    // Create bind group
    if (uniformBuffer && renderBindGroupLayout) {
        WGPUBindGroupEntry bgEntry = {};
        bgEntry.binding = 0;
        bgEntry.buffer = uniformBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(UniformData);

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = renderBindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;

        printf("Before creating bind group. Layout: %p, Buffer: %p\n", 
               (void*)renderBindGroupLayout, (void*)uniformBuffer);
        renderBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        printf("After creating bind group: %p\n", (void*)renderBindGroup);
    } else {
        printf("Failed preconditions for bind group. Layout: %p, Buffer: %p\n",
               (void*)renderBindGroupLayout, (void*)uniformBuffer);
    }

    // Create shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
        struct Uniforms {
            viewProj: mat4x4<f32>,
        }
        @binding(0) @group(0) var<uniform> uniforms: Uniforms;

        struct VertexInput {
            @location(0) position: vec3f,
        };

        struct VertexOutput {
            @builtin(position) position: vec4f,
            @location(0) pointSize: f32,
        };

        @vertex
        fn vs_main(in: VertexInput) -> VertexOutput {
            var out: VertexOutput;
            let worldPos = vec4f(in.position, 1.0);
            out.position = uniforms.viewProj * worldPos;
            
            // Calculate point size based on distance to camera
            let distanceToCamera = out.position.w;
            out.pointSize = max(20.0 / distanceToCamera, 2.0);
            
            return out;
        }

        @fragment
        fn fs_main(@builtin(position) pos: vec4f, @location(0) pointSize: f32) -> @location(0) vec4f {
            // Calculate distance from fragment to point center
            let center = vec2f(pos.x, pos.y);
            let dist = length(center - floor(center));
            
            // Create a soft circular point
            let fade = 1.0 - smoothstep(0.4, 0.5, dist);
            
            // Use white color with soft edges
            return vec4f(1.0, 1.0, 1.0, fade);
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &renderBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Set up vertex attributes and buffer layout
    WGPUVertexAttribute attribute = {};
    attribute.format = WGPUVertexFormat_Float32x3;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Point);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &attribute;

    // Fragment state
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

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.layout = pipelineLayout;

    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    multisample.alphaToCoverageEnabled = false;
    pipelineDesc.multisample = multisample;

    renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Cleanup
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}


void PointWebSystem::createComputePipeline() {
    // Create compute bind group layout
    WGPUBindGroupLayoutEntry computeEntries[2] = {};
    // Input buffer - Change to read-only storage 
    computeEntries[0].binding = 0;
    computeEntries[0].visibility = WGPUShaderStage_Compute;
    computeEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    // Output buffer - This remains read-write storage
    computeEntries[1].binding = 1;
    computeEntries[1].visibility = WGPUShaderStage_Compute;
    computeEntries[1].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor computeBglDesc = {};
    computeBglDesc.entryCount = 2;
    computeBglDesc.entries = computeEntries;
    computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &computeBglDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor computeLayoutDesc = {};
    computeLayoutDesc.bindGroupLayoutCount = 1;
    computeLayoutDesc.bindGroupLayouts = &computeBindGroupLayout;
    WGPUPipelineLayout computePipelineLayout = wgpuDeviceCreatePipelineLayout(device, &computeLayoutDesc);

    // Create compute shader
    WGPUShaderModuleWGSLDescriptor computeWGSLDesc = {};
    computeWGSLDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    computeWGSLDesc.code = R"(
        struct Point {
            position: vec3f,
        }

        @group(0) @binding(0) var<storage, read> input: array<Point>;
        @group(0) @binding(1) var<storage, read_write> output: array<Point>;

        @compute @workgroup_size(256)
        fn main(@builtin(global_invocation_id) global_id : vec3u) {
            let index = global_id.x;
            if (index >= arrayLength(&input)) {
                return;
            }

            // For now, just copy the position
            output[index].position = input[index].position;
        }
    )";

    WGPUShaderModuleDescriptor computeShaderDesc = {};
    computeShaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&computeWGSLDesc);
    WGPUShaderModule computeShaderModule = wgpuDeviceCreateShaderModule(device, &computeShaderDesc);

    // Create compute pipeline
    WGPUComputePipelineDescriptor computePipelineDesc = {};
    computePipelineDesc.layout = computePipelineLayout;
    computePipelineDesc.compute.module = computeShaderModule;
    computePipelineDesc.compute.entryPoint = "main";

    computePipeline = wgpuDeviceCreateComputePipeline(device, &computePipelineDesc);

    // Cleanup
    wgpuShaderModuleRelease(computeShaderModule);
    wgpuPipelineLayoutRelease(computePipelineLayout);
}


void PointWebSystem::createBuffers() {
    // Create vertex buffers for double buffering
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(Point) * points.size();
    // Update usage flags to include read-only storage
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    // Create and initialize buffer A
    vertexBufferA = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    void* vertexDataA = wgpuBufferGetMappedRange(vertexBufferA, 0, vertexBufferDesc.size);
    memcpy(vertexDataA, points.data(), vertexBufferDesc.size);
    wgpuBufferUnmap(vertexBufferA);

    // Create buffer B (initially empty)
    vertexBufferB = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    void* vertexDataB = wgpuBufferGetMappedRange(vertexBufferB, 0, vertexBufferDesc.size);
    memcpy(vertexDataB, points.data(), vertexBufferDesc.size);
    wgpuBufferUnmap(vertexBufferB);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(UniformData);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
}

void PointWebSystem::createBindGroups() {
    if (!renderBindGroupLayout) {
        printf("Error: renderBindGroupLayout is null!\n");
        return;
    }

    // Create render bind group
    WGPUBindGroupEntry renderEntry = {};
    renderEntry.binding = 0;
    renderEntry.buffer = uniformBuffer;
    renderEntry.offset = 0;
    renderEntry.size = sizeof(UniformData);

    WGPUBindGroupDescriptor renderBgDesc = {};
    renderBgDesc.layout = renderBindGroupLayout;
    renderBgDesc.entryCount = 1;
    renderBgDesc.entries = &renderEntry;

    printf("Creating render bind group with layout: %p\n", (void*)renderBgDesc.layout);

    renderBindGroup = wgpuDeviceCreateBindGroup(device, &renderBgDesc);
    if (!renderBindGroup) {
        printf("Failed to create render bind group!\n");
        return;
    }

    // Create compute bind groups for double buffering
    WGPUBindGroupEntry computeEntriesA[2] = {};
    computeEntriesA[0].binding = 0;
    computeEntriesA[0].buffer = vertexBufferA;
    computeEntriesA[0].offset = 0;
    computeEntriesA[0].size = sizeof(Point) * NUM_POINTS;
    computeEntriesA[1].binding = 1;
    computeEntriesA[1].buffer = vertexBufferB;
    computeEntriesA[1].offset = 0;
    computeEntriesA[1].size = sizeof(Point) * NUM_POINTS;

    WGPUBindGroupDescriptor computeBgDescA = {};
    computeBgDescA.layout = computeBindGroupLayout;
    computeBgDescA.entryCount = 2;
    computeBgDescA.entries = computeEntriesA;

    computeBindGroupA = wgpuDeviceCreateBindGroup(device, &computeBgDescA);
    if (!computeBindGroupLayout) {
        printf("Error: computeBindGroupLayoutA is null!\n");
        return;
    }

    WGPUBindGroupEntry computeEntriesB[2] = {};
    computeEntriesB[0].binding = 0;
    computeEntriesB[0].buffer = vertexBufferB;
    computeEntriesB[0].offset = 0;
    computeEntriesB[0].size = sizeof(Point) * NUM_POINTS;
    computeEntriesB[1].binding = 1;
    computeEntriesB[1].buffer = vertexBufferA;
    computeEntriesB[1].offset = 0;
    computeEntriesB[1].size = sizeof(Point) * NUM_POINTS;

    WGPUBindGroupDescriptor computeBgDescB = {};
    computeBgDescB.layout = computeBindGroupLayout;
    computeBgDescB.entryCount = 2;
    computeBgDescB.entries = computeEntriesB;

    computeBindGroupB = wgpuDeviceCreateBindGroup(device, &computeBgDescB);
    if (!computeBindGroupLayout) {
        printf("Error: computeBindGroupLayoutB is null!\n");
        return;
    }
}


void PointWebSystem::updateUniforms(const Camera& camera) {
    uniformData.viewProj = camera.getProjection() * camera.getView();
    
    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &uniformData,
        sizeof(UniformData)
    );
}

void PointWebSystem::render(WGPURenderPassEncoder renderPass, const Camera& camera) {
    updateUniforms(camera);
    
    wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, renderBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, 
        useBufferA ? vertexBufferA : vertexBufferB, 0, sizeof(Point) * points.size());
    wgpuRenderPassEncoderDraw(renderPass, NUM_POINTS, 1, 0, 0);

    // Toggle buffers for next frame
    useBufferA = !useBufferA;
}