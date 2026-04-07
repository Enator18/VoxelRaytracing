#define VK_CHECK(x)                                                 \
    do                                                              \
    {                                                               \
        VkResult err = (x);                                         \
        if (err)                                                    \
        {                                                           \
            std::cerr << "Detected Vulkan error: " << err <<        \
            " at line " <<  __LINE__ << " in file " << __FILE__     \
            << std::endl;                                           \
            abort();                                                \
        }                                                           \
    } while (0)

#include <iostream>
#include <array>

#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <volk.h>
#include <VkBootstrap.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "main.h"
#include "render_vk.h"

using namespace glm;

constexpr u32 NUM_FRAMES = 2;

VkInstance instance;
VkDebugUtilsMessengerEXT debugMessenger;
VkSurfaceKHR renderSurface;
VkPhysicalDevice physDevice;
VkDevice device;
VkQueue graphicsQueue;
u32 graphicsQueueFamily;
VmaAllocator deviceAllocator;
VkSwapchainKHR swapchain;
std::vector<VkImage> swapImages;
std::vector<VkImageView> swapImageViews;
VkCommandPool mainCommandPool;

struct FrameData
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore acquireSemaphore;
    VkFence renderFence;

    VkBuffer mapBuffer;
    VmaAllocation mapAllocation;
    VkDeviceAddress mapAddress;
};

std::array<FrameData, NUM_FRAMES> frames;
std::vector<VkSemaphore> renderSemaphores;
VkDescriptorPool descriptorPool;
VkDescriptorSetLayout descriptorLayout;
VkDescriptorSet descriptorSet;
VkSampler sampler;

struct PushConstants
{
    mat4 playerRot;
    vec2 planeSize;
    vec4 playerPos;
    VkDeviceAddress mapBuffer;
};

VkPipelineLayout pipelineLayout;
VkPipeline pipeline;

u32 frameNum = 0;

struct Texture
{
    VkImage image;
    VmaAllocation allocation;
    VkImageView imageView;
};

std::vector<Texture> textureHandles;

VkImageMemoryBarrier2 ImageBarrier(VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage)
{
    VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    imageBarrier.pNext = nullptr;

    imageBarrier.srcStageMask = srcStage;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = dstStage;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = currentLayout;
    imageBarrier.newLayout = newLayout;

    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || currentLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
            ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageSubresourceRange subImage {};
    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    imageBarrier.subresourceRange = subImage;
    imageBarrier.image = image;

    return imageBarrier;
}

void RenderInitVk()
{
    volkInitializeCustom((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr());

    // Create Vulkan instance
    vkb::InstanceBuilder builder{vkGetInstanceProcAddr};
    vkb::Instance vkbInstance = builder
            .set_app_name("Voxel Raytracer")
            .request_validation_layers()
            .use_default_debug_messenger()
            .require_api_version(1, 3)
            .build().value();


    instance = vkbInstance.instance;
    volkLoadInstanceOnly(instance);
    debugMessenger = vkbInstance.debug_messenger;


    // Create window surface
    SDL_Vulkan_CreateSurface(window, instance, vkbInstance.allocation_callbacks, &renderSurface);

    VkPhysicalDeviceVulkan12Features feat12
    {
        .storageBuffer8BitAccess = true,
        .shaderInt8 = true,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingSampledImageUpdateAfterBind = true,
        .descriptorBindingPartiallyBound = true,
        .runtimeDescriptorArray = true,
        .scalarBlockLayout = true,
        .bufferDeviceAddress = true
    };

    VkPhysicalDeviceVulkan13Features feat13{.synchronization2 = true, .dynamicRendering = true};

    // Select which GPU to use
    vkb::PhysicalDeviceSelector selector{vkbInstance};
    vkb::PhysicalDevice vkbPhysDevice = selector
            .set_surface(renderSurface)
            .set_minimum_version(1, 3)
            .set_required_features_12(feat12)
            .set_required_features_13(feat13)
            .prefer_gpu_device_type()
            .select().value();
    physDevice = vkbPhysDevice.physical_device;


    // Create the logical GPU device
    vkb::DeviceBuilder deviceBuilder{vkbPhysDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();
    device = vkbDevice.device;
    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    volkLoadDevice(device);

    // Create the VMA deviceAllocator
    VmaVulkanFunctions vmaFuncs
    {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr
    };

    VmaAllocatorCreateInfo allocInfo
    {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = physDevice,
        .device = device,
        .pVulkanFunctions = &vmaFuncs,
        .instance = instance
    };

    VK_CHECK(vmaCreateAllocator(&allocInfo, &deviceAllocator));

    // Create the swapchain and associated resources
    vkb::SwapchainBuilder swapBuilder{physDevice, device, renderSurface};

    vkb::Swapchain vkbSwapchain = swapBuilder
            .set_desired_format(VkSurfaceFormatKHR{.format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(WINDOW_WIDTH, WINDOW_HEIGHT)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build().value();

    swapchain = vkbSwapchain.swapchain;
    swapImages = vkbSwapchain.get_images().value();
    swapImageViews = vkbSwapchain.get_image_views().value();

    // Create the command pools and command buffers
    VkCommandPoolCreateInfo commandPoolInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily
    };

    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &mainCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    // Create the sync structures
    VkFenceCreateInfo fenceInfo
    {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkSemaphoreCreateInfo semaphoreInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .flags = 0
    };

    VkBufferCreateInfo mapBufferInfo = {};
    mapBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    mapBufferInfo.pNext = nullptr;

    mapBufferInfo.size = MAP_SIZE * MAP_SIZE * MAP_SIZE * MAP_SIZE;
    mapBufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo mapAllocInfo = {};
    mapAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
        | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    mapAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mapAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    for (int i = 0; i < NUM_FRAMES; i++)
    {
        VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool));

        cmdAllocInfo.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &frames[i].commandBuffer));

        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frames[i].acquireSemaphore));

        VK_CHECK(vmaCreateBuffer(deviceAllocator, &mapBufferInfo, &mapAllocInfo, &frames[i].mapBuffer, &frames[i].mapAllocation, nullptr));

        VkBufferDeviceAddressInfo mapAddressInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = frames[i].mapBuffer
        };
        frames[i].mapAddress = vkGetBufferDeviceAddress(device, &mapAddressInfo);
    }

    renderSemaphores = std::vector<VkSemaphore>(swapImages.size());

    for (int i = 0; i < swapImages.size(); i++)
    {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderSemaphores[i]));
    }

    VkShaderModuleCreateInfo shaderInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    void* shaderFile = SDL_LoadFile("../shaders/render.vert.spv", &shaderInfo.codeSize);
    if (!shaderFile)
    {
        std::cerr << "Failed to load shader file: " << SDL_GetError() << std::endl;
    }
    shaderInfo.pCode = (u32*)shaderFile;
    VkShaderModule vertShader;
    VK_CHECK(vkCreateShaderModule(device, &shaderInfo, nullptr, &vertShader));
    SDL_free(shaderFile);
    VkPipelineShaderStageCreateInfo stages[2];
    stages[0] =
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShader,
        .pName = "main"
    };

    shaderFile = SDL_LoadFile("../shaders/render.frag.spv", &shaderInfo.codeSize);
    if (!shaderFile)
    {
        std::cerr << "Failed to load shader file: " << SDL_GetError() << std::endl;
        abort();
    }
    shaderInfo.pCode = (u32*)shaderFile;
    VkShaderModule fragShader;
    VK_CHECK(vkCreateShaderModule(device, &shaderInfo, nullptr, &fragShader));
    SDL_free(shaderFile);
    stages[1] =
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShader,
        .pName = "main"
    };

    // Set up descriptor pool and set for textures
    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};

    VkDescriptorPoolCreateInfo descriptorPoolInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
            | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 2,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };

    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

    VkDescriptorBindingFlags bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorBindingFlags bindingFlags[2] = {bindlessFlags, 0};
    VkDescriptorSetLayoutBinding bindings[2];
    bindings[0] =
    {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = 4,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };

    bindings[1] =
    {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 2,
        .pBindingFlags = bindingFlags
    };

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings
    };

    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorLayout));

    VkDescriptorSetAllocateInfo descriptorAllocInfo
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorLayout
    };

    VK_CHECK(vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSet));

    VkSamplerCreateInfo samplerInfo
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .compareEnable = VK_FALSE
    };

    vkCreateSampler(device, &samplerInfo, nullptr, &sampler);

    VkDescriptorImageInfo samplerDescInfo{.sampler = sampler};

    VkWriteDescriptorSet samplerWrite
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &samplerDescInfo
    };

    vkUpdateDescriptorSets(device, 1, &samplerWrite, 0, nullptr);

    VkPushConstantRange pushConstants
    {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)
    };

    VkPipelineLayoutCreateInfo depthLayoutInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstants
    };

    VK_CHECK(vkCreatePipelineLayout(device, &depthLayoutInfo, nullptr, &pipelineLayout));

    VkPipelineDynamicStateCreateInfo dynamicState{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

    VkPipelineVertexInputStateCreateInfo vertexInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkViewport viewport{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 1};
    VkRect2D scissor{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};

    VkPipelineViewportStateCreateInfo viewportState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizer
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment
    {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending
    {
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil
    {
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
    };

    VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;

    VkPipelineRenderingCreateInfo renderInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderInfo,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0
    };

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}

void LoadTextureVk(const char* path, u32 index)
{
    SDL_Surface* raw = SDL_LoadPNG(path);
    SDL_Surface* texSurface = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(raw);

    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = nullptr;

    info.imageType = VK_IMAGE_TYPE_3D;
    info.format = VK_FORMAT_R8G8B8A8_SRGB;
    info.flags = 0;
    info.extent = {16, 16, 16};

    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Texture texture;
    VK_CHECK(vmaCreateImage(deviceAllocator, &info, &allocInfo, &texture.image, &texture.allocation, nullptr));

    VkImageViewCreateInfo texViewInfo
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VK_CHECK(vkCreateImageView(device, &texViewInfo, nullptr, &texture.imageView));
    textureHandles.push_back(texture);

    VkDescriptorImageInfo imageInfo{.imageView = texture.imageView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet descriptorWrite
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    size_t dataSize = texSurface->h * texSurface->pitch;

    VkBufferCreateInfo stagingInfo = {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.pNext = nullptr;

    stagingInfo.size = dataSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    stagingAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VK_CHECK(vmaCreateBuffer(deviceAllocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr));

    void* uploadData;
    vmaMapMemory(deviceAllocator, stagingAllocation, &uploadData);
    memcpy(uploadData, texSurface->pixels, dataSize);
    vmaUnmapMemory(deviceAllocator, stagingAllocation);

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = mainCommandPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    VkImageMemoryBarrier2 imageBarrier = ImageBarrier(texture.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COPY_BIT);
    VkDependencyInfo depInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &imageBarrier
    };

    vkCmdPipelineBarrier2(commandBuffer, &depInfo);

    VkBufferImageCopy copyRegion
    {
        .bufferOffset = 0,
        .bufferRowLength = (u32)texSurface->pitch / 4,
        .bufferImageHeight = 0,
        .imageSubresource
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent{16, 16, 16}
    };

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    imageBarrier = ImageBarrier(texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = commandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;

    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphicsQueue));

    vkFreeCommandBuffers(device, mainCommandPool, 1, &commandBuffer);

    vmaDestroyBuffer(deviceAllocator, stagingBuffer, stagingAllocation);
    SDL_DestroySurface(raw);
}

void DrawViewVk()
{
    //Set up commands
    VkCommandBuffer& cmd = frames[frameNum].commandBuffer;

    //Synchronize and get images
    VK_CHECK(vkWaitForFences(device, 1, &frames[frameNum].renderFence, true, 1000000000));

    u32 swapIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, 1000000000, frames[frameNum].acquireSemaphore, VK_NULL_HANDLE, &swapIndex);

    VK_CHECK(vkResetFences(device, 1, &frames[frameNum].renderFence));

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    //Begin commands and rendering
    VkCommandBufferBeginInfo beginInfo
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    f32 planeHeight = tan(radians(fov / 2));
    f32 planeWidth = planeHeight * WINDOW_WIDTH / WINDOW_HEIGHT;

    PushConstants pushConstants
    {
        PlayerRotation(),
        {planeWidth, planeHeight},
        playerPos,
        frames[frameNum].mapAddress
    };

    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);

    void* mapData = frames[frameNum].mapAllocation->GetMappedData();
    memcpy(mapData, map.data(), map.size());

    VkImageMemoryBarrier2 imageDrawBarrier = ImageBarrier(swapImages[swapIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);

    VkClearValue clearValue{.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingAttachmentInfo colorAttachment
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapImageViews[swapIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearValue
    };

    VkRenderingInfo renderInfo
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea
        {
            .offset = {0, 0},
            .extent = {WINDOW_WIDTH, WINDOW_HEIGHT}
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr
    };

    VkDependencyInfo drawDepInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &imageDrawBarrier
    };

    vkCmdPipelineBarrier2(cmd, &drawDepInfo);
    vkCmdBeginRendering(cmd, &renderInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 imagePresentBarrier = ImageBarrier(swapImages[swapIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    VkDependencyInfo presentDepInfo{};
    presentDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    presentDepInfo.pNext = nullptr;
    presentDepInfo.imageMemoryBarrierCount = 1;
    presentDepInfo.pImageMemoryBarriers = &imagePresentBarrier;
    vkCmdPipelineBarrier2(cmd, &presentDepInfo);

    VK_CHECK(vkEndCommandBuffer(cmd));

    //Submit commands
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frames[frameNum].acquireSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderSemaphores[swapIndex]
    };

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, frames[frameNum].renderFence));

    //Draw to screen
    VkPresentInfoKHR presentInfo
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderSemaphores[swapIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &swapIndex
    };

    VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);

    frameNum++;
    frameNum %= NUM_FRAMES;
}