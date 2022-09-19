// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <functional>
#include <deque>
#include "vk_mesh.h"
#include <glm/glm.hpp>
#include <unordered_map>

struct MeshPushConstants {
    glm::vec4 data;
    glm::mat4 render_matrix;
};

struct Material {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
};

//a simple way to hold content for scenes
struct RenderObject {
    Mesh* mesh;

    Material* material;

    glm::mat4 transformMatrix;
};




//FIFO Queue to house Vulkan objects for deletion
struct DeletionQueue{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function) {
        deletors.push_back(function);
    }

    void flush() {
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }
        deletors.clear();
    }

};

// pipelines

class PipelineBuilder {

public:
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkViewport _viewport;
    VkRect2D _scissor;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    VkPipelineColorBlendAttachmentState _colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo _depthStencil;

    VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};


class VulkanEngine {
public:

    VkSwapchainKHR  _swapchain;
    // image format expected by the windowing system
    VkFormat _swapchainImageFormat;

    //array of images from the swapchain
    std::vector<VkImage> _swapchainImages;

    //array of image-views from the swapchain
    std::vector<VkImageView> _swapchainImageViews;

    VkInstance _instance; // Vulkan api context
    VkDebugUtilsMessengerEXT _debug_messenger; //Debug handle
    VkPhysicalDevice _chosenGPU; // physical device
    VkDevice _device;  // vulkan interface to chosen device
    VkSurfaceKHR _surface; // chosen window surface

    VkQueue _graphicsQueue; //queue to submit too
    uint32_t _graphicsQueueFamily; //family of that queue

    VkCommandPool _commandPool; //holds commands to issue
    VkCommandBuffer _mainCommandBuffer; //buffer to execute commands

    VkRenderPass _renderPass;
    std::vector<VkFramebuffer> _framebuffers;

    VkSemaphore _presentSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkPipelineLayout _trianglePipelineLayout;

    VkPipeline _trianglePipeline;
    VkPipeline _redTrianglePipeline;

    DeletionQueue _mainDeletionQueue;

    VmaAllocator _allocator;

    VkPipeline _meshPipeline;
    Mesh _triangleMesh;
    Mesh _monkeyMesh;

    VkPipelineLayout _meshPipelineLayout;

    VkImageView _depthImageView;
    AllocatedImage _depthImage;

    VkFormat _depthFormat;

    std::vector<RenderObject> _renderables;

    std::unordered_map<std::string, Material> _materials;
    std::unordered_map<std::string, Mesh> _meshes;

    int _selectedShader{0};

	bool _isInitialized{ false };
	int _frameNumber {0};

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

    Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

    Material* get_material(const std::string& name);

    Mesh* get_mesh(const std::string& name);

    void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

    void init_scene();

private:

    void init_swapchain();

    void init_vulkan();

    void init_commands();

    void init_default_renderpass();

    void init_framebuffers();

    void init_sync_structures();

    bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

    void init_pipelines();

    void load_meshes();

    void upload_mesh(Mesh& mesh);

};
