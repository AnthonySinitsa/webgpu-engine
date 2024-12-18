#include "PointWebSystem.h"
#include <cstring>

PointWebSystem::PointWebSystem(WGPUDevice device) : device(device) {
    initPoints();
    createPipelineAndResources();
}

PointWebSystem::~PointWebSystem() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
}

void PointWebSystem::initPoints() {
    points.resize(NUM_POINTS);
    
    // Calculate angle step between points
    const float angleStep = 2.0f * M_PI / NUM_POINTS;
    
    for (int i = 0; i < NUM_POINTS; i++) {
        float angle = i * angleStep;
        // Position points in a circle on the XZ plane (y = 0)
        points[i].position[0] = CIRCLE_RADIUS * std::cos(angle); // x = r * cos(θ)
        points[i].position[1] = -1.0f;                           // y = 0
        points[i].position[2] = CIRCLE_RADIUS * std::sin(angle); // z = r * sin(θ)
    }
}

void PointWebSystem::createPipelineAndResources() {
    // Create bind group layout first
    WGPUBindGroupLayoutEntry bglEntry = {};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.hasDynamicOffset = false;
    bglEntry.buffer.minBindingSize = sizeof(UniformData);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Create shader
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
        struct Uniforms {
            viewProj: mat4x4<f32>,
            time: f32,
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
            
            // Calculate rotation angle based on initial position and time
            let baseAngle = atan2(in.position.z, in.position.x);
            let rotationSpeed = 2.0 * 3.14159 * 0.5; // One rotation every 2 seconds
            let angle = baseAngle + uniforms.time * rotationSpeed;
            
            // Calculate new position
            let radius = length(vec2f(in.position.x, in.position.z));
            let rotatedPos = vec3f(
                radius * cos(angle),
                in.position.y,
                radius * sin(angle)
            );
            
            let worldPos = vec4f(rotatedPos, 1.0);
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
            
            // Brighter center, fade to edges
            let color = vec3f(1.0, 1.0, 1.0);  // White base color
            return vec4f(color, fade);
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Set up vertex buffer layout
    WGPUVertexAttribute vertexAttrib = {};
    vertexAttrib.format = WGPUVertexFormat_Float32x3;
    vertexAttrib.offset = 0;
    vertexAttrib.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Point);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &vertexAttrib;

    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    
    // Vertex state
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    // Fragment state
    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragment.targets = &colorTarget;
    pipelineDesc.fragment = &fragment;

    // Other states
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample = multisample;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    // Cleanup
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    // Create buffers and bind group
    createBuffers();
    createBindGroup();
}

void PointWebSystem::createBuffers() {
    // Create vertex buffer
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(Point) * points.size();
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.mappedAtCreation = true;
    vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    
    void* vertexData = wgpuBufferGetMappedRange(vertexBuffer, 0, vertexBufferDesc.size);
    memcpy(vertexData, points.data(), vertexBufferDesc.size);
    wgpuBufferUnmap(vertexBuffer);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(UniformData);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
}

void PointWebSystem::createBindGroup() {
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


void PointWebSystem::update(float deltaTime) {
    currentTime += deltaTime;
    // Keep time within a reasonable range to avoid floating point precision issues
    if (currentTime > 1000.0f) {
        currentTime -= 1000.0f;
    }
}


void PointWebSystem::updateUniforms(const Camera& camera) {
    uniformData.viewProj = camera.getProjection() * camera.getView();
    uniformData.time = currentTime;
    
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
    
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, sizeof(Point) * points.size());
    wgpuRenderPassEncoderDraw(renderPass, NUM_POINTS, 1, 0, 0);
}