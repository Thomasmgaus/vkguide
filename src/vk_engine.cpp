#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include "VkBootstrap.h"

#include<iostream>
#include<fstream>
#include<stdlib.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <glm/gtx/transform.hpp>


using namespace  std;

const uint32_t ONE_SECOND_TIMEOUT = 1000000000;

#define VK_CHECK(x)                                                         \
    do                                                                      \
    {                                                                       \
        VkResult err = x;                                                   \
        if(err) {                                                           \
            std::cout << "Encountered Vulkan Error: " << err << std::endl;  \
            abort();                                                        \
        }                                                                   \
                                                                            \
    } while (0)                                                             \


void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
    std::cout << "SDL init" << std::endl;
	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

    //load core vulkan structure
    init_vulkan();
    std::cout << "Vulkan initialized" << std::endl;
    //create swapchain
    init_swapchain();
    //create command buffer
    init_commands();

    init_default_renderpass();
    std::cout << "past Renderpass" << std::endl;

    // we need to create the renderpass before framebuffers
    // framebuffers are created for specific renderpass
    init_framebuffers();

    init_sync_structures();

    init_descriptors();
    std::cout << "Descriptors initialized" << endl;
    init_pipelines();
    std::cout << "Past Pipelines" << std::endl;
    load_meshes();
    std::cout << "Load scene" << std::endl;
    init_scene();
	
	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_swapchain() {
    vkb::SwapchainBuilder swapchainBuilder{_chosenGPU,_device,_surface};

    vkb::Swapchain vkbSwapchain = swapchainBuilder
            .use_default_format_selection()
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)                 //how will the images render to the swapchain?
            .set_desired_extent(_windowExtent.width, _windowExtent.height)     //send the window description
            .build()
            .value();

    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();

    _swapchainImageFormat = vkbSwapchain.image_format;

    VkExtent3D depthImageExtent = {
            _windowExtent.width,
            _windowExtent.height,
            1
    };

    _depthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

    //allocate the depth image from GPU local memory
    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    //Force allocation to GPU VRAM
    dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate and create the image
    vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage._image, &_depthImage._allocation, nullptr);

    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);
    // the image is now created, and will be hooked into the renderpass
    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImageView));

    _mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(_device, _depthImageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    });


}

void VulkanEngine::init_vulkan() {
    std::cout << "Starting Vulkan Initialization" << std::endl;
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Example Vulkan Application")
            .request_validation_layers(true)
            .require_api_version(1,1,0)
            .use_default_debug_messenger()
            .build();

    vkb::Instance vkb_inst = inst_ret.value(); // result of the builder
    // class instance
    _instance = vkb_inst.instance;
    // store the debug messenger
    _debug_messenger = vkb_inst.debug_messenger;
    std::cout << "debug util started" << std::endl;
    // get the window surface from SDL
    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);
    std::cout << "Surface created" << std::endl;
    //select a GUP capable of writing to an SDL surface
    vkb::PhysicalDeviceSelector selector{ vkb_inst};
    vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1,1) //requested version
            .set_surface(_surface)                //SDL draw surface
            .select()
            .value();

    // build the VkDevice from the physical device
    vkb::DeviceBuilder deviceBuilder {physicalDevice};

    vkb::Device vkbDevice = deviceBuilder.build().value();

    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void VulkanEngine::init_commands() {

    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for(int i = 0; i < FRAME_OVERLAP; i++) {

        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

        _mainDeletionQueue.push_function([=]() {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
        });
    }
}
// Renderpass -> builds images to display to the swapchain

/*
 * General image lifespan flow
 *
 * UNDEFINED -> RenderPass Begins -> Subpass 0 begins (Transition to Attachment Optimal) ->
 * Subpass 0 renders -> Subpass 0 ends -> Renderpass Ends (Transitions to Present Source)
 */
void VulkanEngine::init_default_renderpass() {
    VkAttachmentDescription color_attachment = {};

    //format has to be the same as the swapchain image format
    color_attachment.format = _swapchainImageFormat;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Subpass that will render into the image defined by the renderpass
    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.flags = 0;
    depth_attachment.format = _depthFormat;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    //There must be a minimum of one subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkAttachmentDescription attachments[2] {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = &attachments[0];
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency depth_dependency = {};
    depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depth_dependency.dstSubpass = 0;
    depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depth_dependency.srcAccessMask = 0;
    depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency dependencies[2] = {dependency, depth_dependency};

    render_pass_info.dependencyCount = 2;
    render_pass_info.pDependencies = &dependencies[0];

    VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));

    _mainDeletionQueue.push_function([=]() {
        vkDestroyRenderPass(_device, _renderPass, nullptr);
    });


}
/*
 * Framebuffers serve as the link form the image created from the renderpass to the real image
 */
void VulkanEngine::init_framebuffers() {
    VkFramebufferCreateInfo fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.pNext = nullptr;

    fb_info.renderPass = _renderPass;
    fb_info.attachmentCount = 1;
    fb_info.width = _windowExtent.width;
    fb_info.height = _windowExtent.height;
    fb_info.layers = 1;

    const uint32_t swapchain_imagecount = _swapchainImages.size();
    _framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

    for(int i = 0; i < swapchain_imagecount; i++) {
        VkImageView attachments[2];
        attachments[0] = _swapchainImageViews[i];
        attachments[1] = _depthImageView;

        fb_info.pAttachments = attachments;
        fb_info.attachmentCount = 2;
        VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

        _mainDeletionQueue.push_function([=] (){
            vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
            vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
        });
    }


}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;

    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;

    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer._buffer, &newBuffer._allocation, nullptr));

    return newBuffer;
}

void VulkanEngine::init_sync_structures() {
    // for cpu and gpu sync
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++){
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));


        // didn't follow guide exactly on this, could be a point of errors getting thrown
        _mainDeletionQueue.push_function([=] () {

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
        });
    }
}

void VulkanEngine::init_descriptors() {
    std::vector<VkDescriptorPoolSize> sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10}
    };

    VkDescriptorPoolCreateInfo pool_info = {};

    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = 10;
    pool_info.poolSizeCount = (uint32_t)sizes.size();
    pool_info.pPoolSizes = sizes.data();

    vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool);


    VkDescriptorSetLayoutBinding camBufferBinding = {};
    camBufferBinding.binding = 0;
    camBufferBinding.descriptorCount = 1;
    camBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    camBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo setinfo = {};

    setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setinfo.pNext = nullptr;

    setinfo.bindingCount = 1;
    setinfo.flags = 0;
    setinfo.pBindings = &camBufferBinding;

    vkCreateDescriptorSetLayout(_device, &setinfo, nullptr, &_globalSetLayout);

    for(int i = 0; i < FRAME_OVERLAP; i++) {
        _frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.pNext = nullptr;
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = _descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &_globalSetLayout;

        vkAllocateDescriptorSets(_device, &allocateInfo, &_frames[i].globalDescriptor);

        VkDescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = _frames[i].cameraBuffer._buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUCameraData);

        VkWriteDescriptorSet setWrite = {};
        setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        setWrite.pNext = nullptr;
        setWrite.dstBinding = 0;
        setWrite.dstSet = _frames[i].globalDescriptor;
        setWrite.descriptorCount = 1;
        setWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        setWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(_device, 1, &setWrite, 0, nullptr);
    }

    for(int i = 0; i < FRAME_OVERLAP; i++) {
        _mainDeletionQueue.push_function([&]() {
           vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
        });
    }

    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device,_globalSetLayout, nullptr);
    });

    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorPool(_device,_descriptorPool, nullptr);
    });
}


bool VulkanEngine::load_shader_module(const char *filePath, VkShaderModule *outShaderModule) {


    // std::ios::ate -> put file cursor at the end of the file
    // std::ios::binary -> read the file in as binary
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if(!file.is_open()) {
        return false;
    }

    // cursor is at the end, so itll tell us the file size in bytes
    size_t fileSize = (size_t)file.tellg();
    //spriv expects the buffer type to be of uint32
    std::vector<uint32_t> buffer(fileSize/sizeof(uint32_t));
    //put cursor at beginning of file
    file.seekg(0);

    //load entire file
    file.read((char*)buffer.data(), fileSize);

    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    //codeSize must be in bytes, so we multiply the size of the buffer by the size of uint32_t
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    VkShaderModule shaderModule;
    if(vkCreateShaderModule(_device, &createInfo, nullptr,&shaderModule) != VK_SUCCESS){
        return false;
    }
    *outShaderModule = shaderModule;
    return true;

}

void VulkanEngine::init_pipelines(){
    VkShaderModule colorMeshShader;
    if(!load_shader_module("../shaders/colored_triangle.frag.spv", &colorMeshShader)){
        std::cout << "Error when building the triangle fragment shader module" << std::endl;
    }
    else {
        std::cout << "Triangle fragment shader successfully loaded" << std::endl;
    }
    VkShaderModule meshVertShader;
    if(!load_shader_module("../shaders/tri_mesh.vert.spv", &meshVertShader)){
        std::cout << "Error when building the Green triangle vertex shader module" << std::endl;
    }
    else {
        std::cout << "Green Triangle fragment vert successfully loaded" << std::endl;
    }

    //build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
    PipelineBuilder pipelineBuilder;

    pipelineBuilder._shaderStages.push_back(
            vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

    pipelineBuilder._shaderStages.push_back(
            vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, colorMeshShader));


    VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

    VkPushConstantRange push_constant;
    // Push constant range start
    push_constant.offset = 0;
    // this push constant range takes up the size of MeshPushConstants
    push_constant.size = sizeof(MeshPushConstants);
    // only accessible during the shader vertex
    push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
    mesh_pipeline_layout_info.pushConstantRangeCount = 1;

    mesh_pipeline_layout_info.setLayoutCount = 1;
    mesh_pipeline_layout_info.pSetLayouts = &_globalSetLayout;

    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_meshPipelineLayout));

    pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	//vertex input controls how to read vertices from vertex buffers. We arent using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we dont use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();


	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//build the mesh pipeline

    VertexInputDescription vertexDescription = Vertex::get_vertex_description();

    pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
    pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

    pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
    pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

    _meshPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

    //build the red triangle now
    pipelineBuilder._shaderStages.clear();

    create_material(_meshPipeline, _meshPipelineLayout, "defaultmesh");

    //destroy shaders
    vkDestroyShaderModule(_device, meshVertShader, nullptr);

    _mainDeletionQueue.push_function([=] () {
        // destroy pipelines
        vkDestroyPipeline(_device, _meshPipeline, nullptr);
        //destroy pipeline layout
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
    });
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;

    viewportState.viewportCount = 1;
    viewportState.pViewports= &_viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &_scissor;

    // not using right now but will use later, must match the fragment shader
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &_colorBlendAttachment;

    //Begin building the pipeline
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.pNext = nullptr;

    pipelineCreateInfo.stageCount = _shaderStages.size();
    pipelineCreateInfo.pStages = _shaderStages.data();
    pipelineCreateInfo.pVertexInputState = &_vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &_inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &_rasterizer;
    pipelineCreateInfo.pMultisampleState = &_multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.layout = _pipelineLayout;
    pipelineCreateInfo.renderPass = pass;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.pDepthStencilState = &_depthStencil;

    VkPipeline newPipeline;
    if(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineCreateInfo,nullptr,&newPipeline) != VK_SUCCESS) {
        std::cout << "Failed to create pipeline" << std::endl;
        return VK_NULL_HANDLE;
    }else {
        return newPipeline;
    }

}



void VulkanEngine::cleanup()
{	
	if (_isInitialized) {

        vkWaitForFences(_device,1,&get_current_frame()._renderFence,true, 1000000000);

        _mainDeletionQueue.flush();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);

        vkDestroyDevice(_device, nullptr);
        vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::load_meshes() {
    // resize the array to 3 members
    _triangleMesh._vertices.resize(3);
    _redTriangleMesh._vertices.resize(3);
    _blueTriangleMesh._vertices.resize(3);

    Mesh baseTriangleMesh = {};
    baseTriangleMesh._vertices.resize(3);

    cout << "Base Mesh Creation" << endl;

    //vertex positions
    baseTriangleMesh._vertices[0].position = {1.f,1.f,0.0f};
    baseTriangleMesh._vertices[1].position = {-1.f,1.f,0.0f};
    baseTriangleMesh._vertices[2].position = {0.f,-1.f, 0.0f};

    _triangleMesh._vertices = baseTriangleMesh._vertices;
    _blueTriangleMesh._vertices = baseTriangleMesh._vertices;
    _redTriangleMesh._vertices = baseTriangleMesh._vertices;

    //vertex colors
    _triangleMesh._vertices[0].color = {0.f, 1.f, 0.0f};
    _triangleMesh._vertices[1].color = {0.f, 1.f, 0.0f};
    _triangleMesh._vertices[2].color = {0.f, 1.f, 0.0f};

    _redTriangleMesh._vertices[0].color = {1.f, 0.f, 0.0f};
    _redTriangleMesh._vertices[1].color = {1.f, 0.f, 0.0f};
    _redTriangleMesh._vertices[2].color = {1.f, 0.f, 0.0f};

    _blueTriangleMesh._vertices[0].color = {0.f, 0.f, 1.0f};
    _blueTriangleMesh._vertices[1].color = {0.f, 0.f, 1.0f};
    _blueTriangleMesh._vertices[2].color = {0.f, 0.f, 1.0f};

    cout << "Triangles loaded " << endl;


    _monkeyMesh.load_from_obj("../assets/monkey_smooth.obj");

    upload_mesh(_triangleMesh);
    upload_mesh(_blueTriangleMesh);
    upload_mesh(_redTriangleMesh);
    upload_mesh(_monkeyMesh);

    _meshes["monkey"] = _monkeyMesh;
    _meshes["triangle"] = _triangleMesh;
    _meshes["redTriangle"] = _redTriangleMesh;
    _meshes["blueTriangle"] = _blueTriangleMesh;
}

void VulkanEngine::upload_mesh(Mesh &mesh) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    // total size in bytes of the buffer
    bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
    // how is the buffer going to be used?
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo vmaAllocationInfo = {};
    //How will the memory be used?
    vmaAllocationInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocationInfo,
                             &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation,
                             nullptr));

    _mainDeletionQueue.push_function([=](){
        vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
    });

    void* data;
    vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);
    memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
    vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);

}

Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string &name) {
    Material mat;
    mat.pipeline = pipeline;
    mat.pipelineLayout = layout;
    _materials[name] = mat;
    return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string &name) {
    auto it = _materials.find(name);
    if(it == _materials.end()) {
        return nullptr;
    }
    else {
        return &(*it).second;
    }
}

Mesh* VulkanEngine::get_mesh(const std::string& name){
    auto it = _meshes.find(name);
    if(it == _meshes.end()){
        return nullptr;
    } else {
        return &(*it).second;
    }
}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject *first, int count) {
    //model view matrix
    //camera view


//    glm::mat4 view = glm::lookAt(_cameraPosition,
//                                 _cameraPosition + _cameraOrigin,
//                                 _cameraUpPosition);
//
//    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 30.0f),
//                                 glm::vec3(0.0f, 0.0f, 0.0f),
//                                 glm::vec3(0.0f, 7.0f, 0.0f));

    glm::vec3 camPos = {0.f, -6.f, -10.f };

    glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);

    //camera projection
    glm::mat4 projection = glm::perspective(glm::radians(70.f),1700.f/ 900.f, 0.1f, 200.0f);
    projection[1][1] *= -1;

    GPUCameraData camData;
    camData.proj = projection;
    camData.view = view;
    camData.viewproj = projection * view;

    void *data;
    vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
    memcpy(data, &camData, sizeof(GPUCameraData));
    vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

    Mesh * lastMesh = nullptr;
    Material* lastMaterial = nullptr;
    for (int i = 0; i < count; i++){
        RenderObject& object = first[i];
        if (object.material != lastMaterial) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material-> pipeline);
            lastMaterial = object.material;
            //bind the descriptor set when changing pipeline
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material -> pipelineLayout, 0, 1, &get_current_frame().globalDescriptor, 0, nullptr);
        }

        glm::mat4 model = object.transformMatrix;

        glm::mat4 mesh_matrix = projection * view * model;

        MeshPushConstants constants;
        constants.render_matrix = object.transformMatrix;

        vkCmdPushConstants(cmd,object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(MeshPushConstants),&constants);

        if(object.mesh != lastMesh) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
            lastMesh = object.mesh;
        }

        vkCmdDraw(cmd, object.mesh->_vertices.size(), 1,0,0);
    }

}

void VulkanEngine::init_scene() {
    RenderObject monkey;
    monkey.mesh = get_mesh("monkey");
    monkey.material = get_material("defaultmesh");
    monkey.transformMatrix = glm::mat4{1.0f};

    _renderables.push_back(monkey);

    for(int x = -30; x <=30;x++){
        for(int y = -30; y <= 30; y++){

            RenderObject tri;
            if(y < -10) tri.mesh = get_mesh("triangle");
            if(y > -10 && y < 10) tri.mesh = get_mesh("redTriangle");
            if(y > 10) tri.mesh = get_mesh("blueTriangle");
            tri.material = get_material("defaultmesh");
            glm::mat4 translation = glm::translate(glm::mat4{1.0},glm::vec3(x,0,y));
            glm::mat4 scale = glm::scale(glm::mat4{1.0}, glm::vec3(0.2,0.2,0.2));
            tri.transformMatrix = translation * scale;

            _renderables.push_back(tri);
        }
    }
}
/**
 *
 *  N frame number modulo 2 will yield either 0 or 1 as the index of frames
 *  Meaning that depending from which frame the gpu is performing the command buffer on, the cpu will be writing and preparing
 *  the next frame into either index 0 or index 1.
 *
 * */
FrameData& VulkanEngine::get_current_frame() {
    return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::draw()
{
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, ONE_SECOND_TIMEOUT));
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

    //Request image from the swapchain
    uint32_t swapchainImageIndex;
    //sent presentSemaphore to check later
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, ONE_SECOND_TIMEOUT, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));
    //empty Command Buffer
    VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    //Begin recording commands
    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.pNext = nullptr;

    cmdBeginInfo.pInheritanceInfo = nullptr;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    VkClearValue clearValue;

    //make a clear-color from frame number. This will flash with a 120*pi frame period.
    clearValue.color = {{0.0f,0.0f,0.0f,1.0f}};

    VkClearValue depthClear;
    depthClear.depthStencil.depth = 1.f;

    //begin renderpass
    VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex]);

    rpInfo.clearValueCount = 2;

    VkClearValue clearValues[] = {clearValue, depthClear };

    rpInfo.pClearValues = &clearValues[0];

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    draw_objects(cmd, _renderables.data(), _renderables.size());

    //finalize the render pass
    vkCmdEndRenderPass(cmd);
    //finalize command buffer
    VK_CHECK(vkEndCommandBuffer(cmd));

    //Prepare the submission to the Queue
    //wait on the semaphore to know that the swapchain is ready

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    submitInfo.pWaitDstStageMask = &waitStage;

    //await the swapChain image
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &get_current_frame()._presentSemaphore;

    //begin rendering
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &get_current_frame()._renderSemaphore;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, get_current_frame()._renderFence));

    //wait on the renderSemaphore so we know the drawing commands are completed and the image is ready to present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;

    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

    //increase number of frames drawn
    _frameNumber++;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{
		//Handle events on queue
        const float cameraSpeed = 0.20f;
		while (SDL_PollEvent(&e) != 0)
		{
			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT) { bQuit = true; }
//            else if (e.type == SDL_KEYDOWN){
//                // which key is it?
//                if(e.key.keysym.sym == SDLK_w) {
//                    _cameraPosition += cameraSpeed * _cameraOrigin;
//                }
//                if(e.key.keysym.sym == SDLK_s) {
//                    _cameraPosition -= cameraSpeed * _cameraOrigin;
//                }
//                if(e.key.keysym.sym == SDLK_d) {
//                    _cameraPosition += glm::normalize(glm::cross(_cameraOrigin, _cameraUpPosition)) * cameraSpeed;
//                }
//                if(e.key.keysym.sym == SDLK_a) {
//                    _cameraPosition -= glm::normalize(glm::cross(_cameraOrigin, _cameraUpPosition)) * cameraSpeed;
//                }
//            }
		}
		draw();
	}
}



