/*
 * Vulkan Example - Texture mapping with transparency using accelerated ray
 * tracing example
 *
 * Copyright (C) 2024-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT)
 * (http://opensource.org/licenses/MIT)
 */

/*
 * This hardware accelerated ray tracing sample renders a texture mapped quad
 * with transparency The sample also makes use of buffer device addresses to
 * pass references for vertex and index buffers to the shader, making data
 * access a bit more straightforward than using descriptors. Buffer references
 * themselves are then simply set at draw time using push constants. In addition
 * to a closest hit shader, that now samples from the texture_, an any hit
 * shader is added to the closest hit shader group. We use this shader to check
 * if the texel we want to sample at the currently hit ray position is
 * transparent, and if that's the case the any hit shader will cancel the
 * intersection.
 */

#include "VulkanRaytracingSample.h"

#include <ktx.h>
#include <ktxvulkan.h>

class VulkanExample : public VulkanRaytracingSample {
 public:
  AccelerationStructure bottomLevelAS_{};
  AccelerationStructure topLevelAS_{};

  vks::Texture cubeMap_;

  vks::Buffer vertexBuffer_;
  vks::Buffer indexBuffer_;
  uint32_t indexCount_{0};
  vks::Buffer transformBuffer_;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups_{};

  struct ShaderBindingTables {
    ShaderBindingTable raygen;
    ShaderBindingTable miss;
    ShaderBindingTable hit;
  } shaderBindingTables_;

  vks::Texture2D texture_;

  struct UniformData {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
  } uniformData_;
  std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers_;

  VkPipeline pipeline_{VK_NULL_HANDLE};
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptorSetLayout_{VK_NULL_HANDLE};
  std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets_{};

  // c'tor
  VulkanExample() : VulkanRaytracingSample() {
    title = "Ray tracing textures";
    camera.type = Camera::CameraType::lookat;
    camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
    camera.setRotation(glm::vec3(45.0f, 0.0f, 0.0f));
    camera.setTranslation(glm::vec3(0.0f, 0.0f, -1.0f));
    enableExtensions();
    // Buffer device address requires the 64-bit integer feature to be enabled
    enabledFeatures.shaderInt64 = VK_TRUE;
  }

  void createAccelerationStructureBuffer(
      AccelerationStructure& accelerationStructure,
      VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo) {
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr,
                                   &accelerationStructure.buffer));
    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, accelerationStructure.buffer,
                                  &memoryRequirements);
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
    memoryAllocateFlagsInfo.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    VkMemoryAllocateInfo memoryAllocateInfo{};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
        memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memoryAllocateInfo, nullptr,
                                     &accelerationStructure.memory));
    VK_CHECK_RESULT(vkBindBufferMemory(device, accelerationStructure.buffer,
                                       accelerationStructure.memory, 0));
  }

  /*
          Create the bottom level acceleration structure contains the scene's
     actual geometry (vertices, triangles)
  */
  void createBottomLevelAccelerationStructure() {
    // Setup vertices for a single triangle
    struct Vertex {
      float pos[3];
      float normal[3];
      float uv[2];
    };
    std::vector<Vertex> vertices = {
        {{0.5f, 0.5f, 0.0f}, {.0f, .0f, -1.0f}, {1.0f, 1.0f}},
        {{-.5f, 0.5f, 0.0f}, {.0f, .0f, -1.0f}, {0.0f, 1.0f}},
        {{-.5f, -.5f, 0.0f}, {.0f, .0f, -1.0f}, {0.0f, 0.0f}},
        {{0.5f, -.5f, 0.0f}, {.0f, .0f, -1.0f}, {1.0f, 0.0f}},
    };

    // Setup indices
    std::vector<uint32_t> indices = {0, 1, 2, 0, 3, 2};
    indexCount_ = static_cast<uint32_t>(indices.size());

    // Setup identity transform matrix
    VkTransformMatrixKHR transformMatrix = {1.0f, 0.0f, 0.0f, 0.0f,

                                            0.0f, 1.0f, 0.0f, 0.0f,

                                            0.0f, 0.0f, 1.0f, 0.0f};

    // Create buffers
    // For the sake of simplicity we won't stage the vertex data to the GPU
    // memory Vertex buffer
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &vertexBuffer_, vertices.size() * sizeof(Vertex), vertices.data()));
    // Index buffer
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &indexBuffer_, indices.size() * sizeof(uint32_t), indices.data()));
    // Transform buffer
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &transformBuffer_, sizeof(VkTransformMatrixKHR), &transformMatrix));

    VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
    VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
    VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

    vertexBufferDeviceAddress.deviceAddress =
        getBufferDeviceAddress(vertexBuffer_.buffer);
    indexBufferDeviceAddress.deviceAddress =
        getBufferDeviceAddress(indexBuffer_.buffer);
    transformBufferDeviceAddress.deviceAddress =
        getBufferDeviceAddress(transformBuffer_.buffer);

    // Build
    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.geometry.triangles.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    accelerationStructureGeometry.geometry.triangles.vertexFormat =
        VK_FORMAT_R32G32B32_SFLOAT;
    accelerationStructureGeometry.geometry.triangles.vertexData =
        vertexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.maxVertex = 3;
    accelerationStructureGeometry.geometry.triangles.vertexStride =
        sizeof(Vertex);
    accelerationStructureGeometry.geometry.triangles.indexType =
        VK_INDEX_TYPE_UINT32;
    accelerationStructureGeometry.geometry.triangles.indexData =
        indexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.transformData
        .deviceAddress = 0;
    accelerationStructureGeometry.geometry.triangles.transformData.hostAddress =
        nullptr;
    accelerationStructureGeometry.geometry.triangles.transformData =
        transformBufferDeviceAddress;

    // Get size info
    VkAccelerationStructureBuildGeometryInfoKHR
        accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries =
        &accelerationStructureGeometry;

    const uint32_t numTriangles = static_cast<uint32_t>(indices.size() / 3);

    VkAccelerationStructureBuildSizesInfoKHR
        accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo, &numTriangles,
        &accelerationStructureBuildSizesInfo);

    createAccelerationStructureBuffer(bottomLevelAS_,
                                      accelerationStructureBuildSizesInfo);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = bottomLevelAS_.buffer;
    accelerationStructureCreateInfo.size =
        accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo,
                                     nullptr, &bottomLevelAS_.handle);

    // Create a small scratch buffer used during build of the bottom level
    // acceleration structure
    ScratchBuffer scratchBuffer = createScratchBuffer(
        accelerationStructureBuildSizesInfo.buildScratchSize);

    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
    accelerationBuildGeometryInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationBuildGeometryInfo.mode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationBuildGeometryInfo.dstAccelerationStructure =
        bottomLevelAS_.handle;
    accelerationBuildGeometryInfo.geometryCount = 1;
    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationBuildGeometryInfo.scratchData.deviceAddress =
        scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR
        accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*>
        accelerationBuildStructureRangeInfos = {
            &accelerationStructureBuildRangeInfo};

    // Build the acceleration structure on the device via a one-time command
    // buffer submission Some implementations may support acceleration structure
    // building on the host
    // (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands),
    // but we prefer device builds
    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBuildAccelerationStructuresKHR(
        commandBuffer, 1, &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());
    vulkanDevice->flushCommandBuffer(commandBuffer, queue);

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = bottomLevelAS_.handle;
    bottomLevelAS_.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(
        device, &accelerationDeviceAddressInfo);

    deleteScratchBuffer(scratchBuffer);
  }

  /*
          The top level acceleration structure contains the scene's object
     instances
  */
  void createTopLevelAccelerationStructure() {
    VkTransformMatrixKHR transformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    VkAccelerationStructureInstanceKHR instance{};
    instance.transform = transformMatrix;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottomLevelAS_.deviceAddress;

    // Buffer for instance data
    vks::Buffer instancesBuffer;
    VK_CHECK_RESULT(vulkanDevice->createBuffer(
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &instancesBuffer, sizeof(VkAccelerationStructureInstanceKHR),
        &instance));

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress =
        getBufferDeviceAddress(instancesBuffer.buffer);

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometry.geometry.instances.data =
        instanceDataDeviceAddress;

    // Get size info
    /*
    The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of
    pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo
    are ignored by this command, except that the hostAddress member of
    VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be
    examined to check if it is NULL.*
    */
    VkAccelerationStructureBuildGeometryInfoKHR
        accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries =
        &accelerationStructureGeometry;

    uint32_t primitive_count = 1;

    VkAccelerationStructureBuildSizesInfoKHR
        accelerationStructureBuildSizesInfo{};
    accelerationStructureBuildSizesInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo, &primitive_count,
        &accelerationStructureBuildSizesInfo);

    createAccelerationStructureBuffer(topLevelAS_,
                                      accelerationStructureBuildSizesInfo);

    VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
    accelerationStructureCreateInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelerationStructureCreateInfo.buffer = topLevelAS_.buffer;
    accelerationStructureCreateInfo.size =
        accelerationStructureBuildSizesInfo.accelerationStructureSize;
    accelerationStructureCreateInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo,
                                     nullptr, &topLevelAS_.handle);

    // Create a small scratch buffer used during build of the top level
    // acceleration structure
    ScratchBuffer scratchBuffer = createScratchBuffer(
        accelerationStructureBuildSizesInfo.buildScratchSize);

    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
    accelerationBuildGeometryInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationBuildGeometryInfo.mode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS_.handle;
    accelerationBuildGeometryInfo.geometryCount = 1;
    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationBuildGeometryInfo.scratchData.deviceAddress =
        scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR
        accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount = 1;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*>
        accelerationBuildStructureRangeInfos = {
            &accelerationStructureBuildRangeInfo};

    // Build the acceleration structure on the device via a one-time command
    // buffer submission Some implementations may support acceleration structure
    // building on the host
    // (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands),
    // but we prefer device builds
    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBuildAccelerationStructuresKHR(
        commandBuffer, 1, &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());
    vulkanDevice->flushCommandBuffer(commandBuffer, queue);

    VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
    accelerationDeviceAddressInfo.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationDeviceAddressInfo.accelerationStructure = topLevelAS_.handle;
    topLevelAS_.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(
        device, &accelerationDeviceAddressInfo);

    deleteScratchBuffer(scratchBuffer);
    instancesBuffer.destroy();
  }

  /*
          Create the Shader Binding Tables that binds the programs and top-level
     acceleration structure

          SBT Layout used in this sample:

                  /-----------\
                  | raygen    |
                  |-----------|
                  | miss      |
                  |-----------|
                  | hit       |
                  \-----------/

  */
  void createShaderBindingTables() {
    const uint32_t handleSize =
        rayTracingPipelineProperties.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = vks::tools::alignedSize(
        rayTracingPipelineProperties.shaderGroupHandleSize,
        rayTracingPipelineProperties.shaderGroupHandleAlignment);
    const uint32_t groupCount = static_cast<uint32_t>(shaderGroups_.size());
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(
        device, pipeline_, 0, groupCount, sbtSize, shaderHandleStorage.data()));

    createShaderBindingTable(shaderBindingTables_.raygen, 1);
    createShaderBindingTable(shaderBindingTables_.miss, 1);
    createShaderBindingTable(shaderBindingTables_.hit, 1);

    // Copy handles
    memcpy(shaderBindingTables_.raygen.mapped, shaderHandleStorage.data(),
           handleSize);
    memcpy(shaderBindingTables_.miss.mapped,
           shaderHandleStorage.data() + handleSizeAligned, handleSize);
    memcpy(shaderBindingTables_.hit.mapped,
           shaderHandleStorage.data() + handleSizeAligned * 2, handleSize);
  }

  /*
          Create the descriptor sets used for the ray tracing dispatch
  */
  void createDescriptorSets() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxConcurrentFrames},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxConcurrentFrames},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames}};
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo =
        vks::initializers::descriptorPoolCreateInfo(poolSizes,
                                                    maxConcurrentFrames);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo,
                                           nullptr, &descriptorPool));

    // Sets per frame, just like the buffers themselves
    // Acceleration structure and images do not need to be duplicated per frame,
    // we use the same for each descriptor to keep things simple
    VkDescriptorSetAllocateInfo allocInfo =
        vks::initializers::descriptorSetAllocateInfo(descriptorPool,
                                                     &descriptorSetLayout_, 1);
    for (auto i = 0; i < maxConcurrentFrames; i++) {
      VK_CHECK_RESULT(
          vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets_[i]));

      // The fragment shader needs access to the ray tracing acceleration
      // structure, so we pass it as a descriptor

      VkWriteDescriptorSetAccelerationStructureKHR
          descriptorAccelerationStructureInfo{};
      descriptorAccelerationStructureInfo.sType =
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
      descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
      descriptorAccelerationStructureInfo.pAccelerationStructures =
          &topLevelAS_.handle;

      VkWriteDescriptorSet accelerationStructureWrite{};
      accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      // The specialized acceleration structure descriptor has to be chained
      accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
      accelerationStructureWrite.dstSet = descriptorSets_[i];
      accelerationStructureWrite.dstBinding = 0;
      accelerationStructureWrite.descriptorCount = 1;
      accelerationStructureWrite.descriptorType =
          VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

      VkDescriptorImageInfo storageImageDescriptor{};
      storageImageDescriptor.imageView = storageImage.view;
      storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

      std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
          // Binding 0: Top level acceleration structure
          accelerationStructureWrite,
          // Binding 1: Ray tracing result image
          vks::initializers::writeDescriptorSet(
              descriptorSets_[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
              &storageImageDescriptor),
          // Binding 2: Uniform data
          vks::initializers::writeDescriptorSet(
              descriptorSets_[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2,
              &uniformBuffers_[i].descriptor),
          // Binding 3: Texture image
          vks::initializers::writeDescriptorSet(
              descriptorSets_[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3,
              &texture_.descriptor),
      };
      vkUpdateDescriptorSets(device,
                             static_cast<uint32_t>(writeDescriptorSets.size()),
                             writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
    }
  }

  /*
          Create our ray tracing pipeline_
  */
  void createRayTracingPipeline() {
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        // Binding 0: Top level acceleration structure
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            0),
        // Binding 1: Ray tracing result image
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            1),
        // Binding 2: Uniform buffer
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                VK_SHADER_STAGE_MISS_BIT_KHR,
            2),
        // Binding 3: Texture image
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            3)};

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI =
        vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(
        device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout_));

    // We pass buffer references for vertex and index buffers via push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(uint64_t) * 2;

    VkPipelineLayoutCreateInfo pipelineLayoutCI =
        vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout_, 1);
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr,
                                           &pipelineLayout_));

    /*
            Setup ray tracing shader groups
    */
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // Ray generation group
    {
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingtextures/raygen.rgen.spv",
                     VK_SHADER_STAGE_RAYGEN_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroup.generalShader =
          static_cast<uint32_t>(shaderStages.size()) - 1;
      shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      shaderGroups_.push_back(shaderGroup);
    }

    // Miss group
    {
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingtextures/miss.rmiss.spv",
                     VK_SHADER_STAGE_MISS_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroup.generalShader =
          static_cast<uint32_t>(shaderStages.size()) - 1;
      shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      shaderGroups_.push_back(shaderGroup);
    }

    // Closest hit group for doing texture_ lookups
    {
      shaderStages.push_back(loadShader(
          getShadersPath() + "raytracingtextures/closesthit.rchit.spv",
          VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type =
          VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.closestHitShader =
          static_cast<uint32_t>(shaderStages.size()) - 1;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      // This group also uses an anyhit shader for doing transparency (see
      // anyhit.rahit for details)
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingtextures/anyhit.rahit.spv",
                     VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
      shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
      shaderGroups_.push_back(shaderGroup);
    }

    /*
            Create the ray tracing pipeline_
    */
    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType =
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount =
        static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount =
        static_cast<uint32_t>(shaderGroups_.size());
    rayTracingPipelineCI.pGroups = shaderGroups_.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
    rayTracingPipelineCI.layout = pipelineLayout_;
    VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(
        device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI,
        nullptr, &pipeline_));
  }

  /*
          Create the uniform buffer used to pass matrices to the ray tracing ray
     generation shader
  */
  void createUniformBuffer() {
    for (auto& buffer : uniformBuffers_) {
      VK_CHECK_RESULT(vulkanDevice->createBuffer(
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          &buffer, sizeof(UniformData), &uniformData_));
      VK_CHECK_RESULT(buffer.map());
    }
  }

  /*
          If the window has been resized, we need to recreate the storage image
     and it's descriptor
  */
  void handleResize() {
    // Recreate image
    createStorageImage(swapChain.colorFormat, {width, height, 1});
    // Update descriptors
    VkDescriptorImageInfo storageImageDescriptor{
        VK_NULL_HANDLE, storageImage.view, VK_IMAGE_LAYOUT_GENERAL};
    for (auto i = 0; i < maxConcurrentFrames; i++) {
      VkWriteDescriptorSet resultImageWrite =
          vks::initializers::writeDescriptorSet(
              descriptorSets_[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
              &storageImageDescriptor);
      vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
    }
    resized = false;
  }

  void updateUniformBuffers() {
    uniformData_.projInverse = glm::inverse(camera.matrices.perspective);
    uniformData_.viewInverse = glm::inverse(camera.matrices.view);
    memcpy(uniformBuffers_[currentBuffer].mapped, &uniformData_,
           sizeof(uniformData_));
  }

  void getEnabledFeatures() {
    // Enable features required for ray tracing using feature chaining via pNext
    enabledBufferDeviceAddresFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

    enabledRayTracingPipelineFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    enabledRayTracingPipelineFeatures.pNext =
        &enabledBufferDeviceAddresFeatures;

    enabledAccelerationStructureFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
    enabledAccelerationStructureFeatures.pNext =
        &enabledRayTracingPipelineFeatures;

    deviceCreatepNextChain = &enabledAccelerationStructureFeatures;
  }

  // Loads a cubemap from a file, uploads it to the device and create all Vulkan
  // resources required to display it
  void loadCubemap(std::string filename, VkFormat format) {
    ktxResult result;
    ktxTexture* ktxTexture;

    if (!vks::tools::fileExists(filename)) {
      vks::tools::exitFatal("Could not load texture_ from " + filename +
                                "\n\nMake sure the assets submodule has been "
                                "checked out and is up-to-date.",
                            -1);
    }
    result = ktxTexture_CreateFromNamedFile(
        filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
    assert(result == KTX_SUCCESS);

    // Get properties required for using and upload texture_ data from the ktx
    // texture_ object
    cubeMap_.width = ktxTexture->baseWidth;
    cubeMap_.height = ktxTexture->baseHeight;
    cubeMap_.mipLevels = ktxTexture->numLevels;
    ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

    VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
    VkMemoryRequirements memReqs;

    // Create a host-visible staging buffer that contains the raw image data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
    bufferCreateInfo.size = ktxTextureSize;
    // This buffer is used as a transfer source for the buffer copy
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(
        vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

    // Get memory requirements for the staging buffer (alignment, memory type
    // bits)
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    // Get memory type index for a host visible buffer
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(
        vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
    VK_CHECK_RESULT(
        vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

    // Copy texture_ data into staging buffer
    uint8_t* data;
    VK_CHECK_RESULT(
        vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void**)&data));
    memcpy(data, ktxTextureData, ktxTextureSize);
    vkUnmapMemory(device, stagingMemory);

    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = cubeMap_.mipLevels;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = {cubeMap_.width, cubeMap_.height, 1};
    imageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    // Cube faces count as array layers in Vulkan
    imageCreateInfo.arrayLayers = 6;
    // This flag is required for cube map images
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VK_CHECK_RESULT(
        vkCreateImage(device, &imageCreateInfo, nullptr, &cubeMap_.image));

    vkGetImageMemoryRequirements(device, cubeMap_.image, &memReqs);

    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
        memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr,
                                     &cubeMap_.deviceMemory));
    VK_CHECK_RESULT(
        vkBindImageMemory(device, cubeMap_.image, cubeMap_.deviceMemory, 0));

    VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // Setup buffer copy regions for each face including all of its miplevels
    std::vector<VkBufferImageCopy> bufferCopyRegions;
    uint32_t offset = 0;

    for (uint32_t face = 0; face < 6; face++) {
      for (uint32_t level = 0; level < cubeMap_.mipLevels; level++) {
        // Calculate offset into staging buffer for the current mip level and
        // face
        ktx_size_t offset;
        KTX_error_code ret =
            ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
        assert(ret == KTX_SUCCESS);
        VkBufferImageCopy bufferCopyRegion = {};
        bufferCopyRegion.imageSubresource.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.mipLevel = level;
        bufferCopyRegion.imageSubresource.baseArrayLayer = face;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
        bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
        bufferCopyRegion.imageExtent.depth = 1;
        bufferCopyRegion.bufferOffset = offset;
        bufferCopyRegions.push_back(bufferCopyRegion);
      }
    }

    // Image barrier for optimal image (target)
    // Set initial layout for all array layers (faces) of the optimal (target)
    // tiled texture_
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = cubeMap_.mipLevels;
    subresourceRange.layerCount = 6;

    vks::tools::setImageLayout(
        copyCmd, cubeMap_.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

    // Copy the cube map faces from the staging buffer to the optimal tiled
    // image
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, cubeMap_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(bufferCopyRegions.size()),
                           bufferCopyRegions.data());

    // Change texture_ image layout to shader read after all faces have been
    // copied
    cubeMap_.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vks::tools::setImageLayout(copyCmd, cubeMap_.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               cubeMap_.imageLayout, subresourceRange);

    vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

    // Create sampler
    VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = sampler.addressModeU;
    sampler.addressModeW = sampler.addressModeU;
    sampler.mipLodBias = 0.0f;
    sampler.compareOp = VK_COMPARE_OP_NEVER;
    sampler.minLod = 0.0f;
    sampler.maxLod = static_cast<float>(cubeMap_.mipLevels);
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler.maxAnisotropy = 1.0f;
    if (vulkanDevice->features.samplerAnisotropy) {
      sampler.maxAnisotropy =
          vulkanDevice->properties.limits.maxSamplerAnisotropy;
      sampler.anisotropyEnable = VK_TRUE;
    }
    VK_CHECK_RESULT(
        vkCreateSampler(device, &sampler, nullptr, &cubeMap_.sampler));

    // Create image view
    VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
    // Cube map view type
    view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // 6 array layers (faces)
    view.subresourceRange.layerCount = 6;
    // Set number of mip levels
    view.subresourceRange.levelCount = cubeMap_.mipLevels;
    view.image = cubeMap_.image;
    VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &cubeMap_.view));

    // Clean up staging resources
    vkFreeMemory(device, stagingMemory, nullptr);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    ktxTexture_Destroy(ktxTexture);
  }

  void loadAssets() {
    texture_.loadFromFile(getAssetPath() + "textures/gratefloor_rgba.ktx",
                          VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
    // Cubemap texture
    loadCubemap(getAssetPath() + "textures/blackhole/skybox/cubemap.ktx",
                VK_FORMAT_R8G8B8A8_UNORM);
  }

  void prepare() {
    VulkanRaytracingSample::prepare();

    loadAssets();

    // Create the acceleration structures used to render the ray traced scene
    createBottomLevelAccelerationStructure();
    createTopLevelAccelerationStructure();

    createStorageImage(swapChain.colorFormat, {width, height, 1});
    createUniformBuffer();
    createRayTracingPipeline();
    createShaderBindingTables();
    createDescriptorSets();
    prepared = true;
  }

  void buildCommandBuffer() {
    if (resized) {
      handleResize();
    }

    VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];

    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                                0, 1};

    VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

    /*
            Dispatch the ray tracing commands
    */
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                      pipeline_);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            pipelineLayout_, 0, 1,
                            &descriptorSets_[currentBuffer], 0, 0);

    struct BufferReferences {
      uint64_t vertices;
      uint64_t indices;
    } bufferReferences{};

    bufferReferences.vertices = getBufferDeviceAddress(vertexBuffer_.buffer);
    bufferReferences.indices = getBufferDeviceAddress(indexBuffer_.buffer);

    // We set the buffer references for the mesh to be rendered using a push
    // constant If we wanted to render multiple objecets this would make it very
    // easy to access their vertex and index buffers
    vkCmdPushConstants(
        cmdBuffer, pipelineLayout_,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
        0, sizeof(uint64_t) * 2, &bufferReferences);

    VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
    vkCmdTraceRaysKHR(cmdBuffer,
                      &shaderBindingTables_.raygen.stridedDeviceAddressRegion,
                      &shaderBindingTables_.miss.stridedDeviceAddressRegion,
                      &shaderBindingTables_.hit.stridedDeviceAddressRegion,
                      &emptySbtEntry, width, height, 1);

    /*
            Copy ray tracing output to swap chain image
    */

    // Prepare current swap chain image as transfer destination
    vks::tools::setImageLayout(cmdBuffer, swapChain.images[currentImageIndex],
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               subresourceRange);

    // Prepare ray tracing output image as transfer source
    vks::tools::setImageLayout(
        cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {width, height, 1};
    vkCmdCopyImage(cmdBuffer, storageImage.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapChain.images[currentImageIndex],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Transition swap chain image back for presentation
    vks::tools::setImageLayout(cmdBuffer, swapChain.images[currentImageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                               subresourceRange);

    // Transition ray tracing output image back to general layout
    vks::tools::setImageLayout(cmdBuffer, storageImage.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_IMAGE_LAYOUT_GENERAL, subresourceRange);

    drawUI(cmdBuffer, frameBuffers[currentImageIndex]);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
  }

  ~VulkanExample() {
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    deleteStorageImage();
    deleteAccelerationStructure(bottomLevelAS_);
    deleteAccelerationStructure(topLevelAS_);
    vertexBuffer_.destroy();
    indexBuffer_.destroy();
    transformBuffer_.destroy();
    shaderBindingTables_.raygen.destroy();
    shaderBindingTables_.miss.destroy();
    shaderBindingTables_.hit.destroy();
    texture_.destroy();
    for (auto& buffer : uniformBuffers_) {
      buffer.destroy();
    }
  }

  virtual void render() {
    if (!prepared) {
      return;
    }
    VulkanExampleBase::prepareFrame();
    updateUniformBuffers();
    buildCommandBuffer();
    VulkanExampleBase::submitFrame();
  }
};

VULKAN_EXAMPLE_MAIN()
