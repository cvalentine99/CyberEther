#include "jetstream/logger.hh"

#include "jetstream/backend/devices/vulkan/base.hh"
#include "jetstream/backend/devices/vulkan/helpers.hh"
#include "jetstream/backend/telemetry.hh"

#include <chrono>
#include <thread>

#if defined(__has_include)
#  if __has_include(<nvml.h>)
#    define JST_BACKEND_HAS_NVML 1
#    include <nvml.h>
#  endif
#endif

#ifndef JST_BACKEND_HAS_NVML
#define JST_BACKEND_HAS_NVML 0
#endif

namespace Jetstream::Backend {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(VkDebugReportFlagsEXT,
                                                           VkDebugReportObjectTypeEXT,
                                                           uint64_t,
                                                           size_t,
                                                           int32_t,
                                                           const char*,
                                                           const char *pMessage,
                                                           void*) {
    JST_DEBUG("[VULKAN] {}", pMessage);
    return VK_FALSE;
}

std::set<std::string> Vulkan::getRequiredInstanceExtensions() {
    std::set<std::string> extensions;

    // Presentation extensions.

    if (!config.headless) {
        extensions.insert(VK_KHR_SURFACE_EXTENSION_NAME);

#if defined(JST_OS_LINUX)
        extensions.insert(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
        if (Backend::WindowMightBeWayland()) {
            extensions.insert(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        }
#endif
#if defined(JST_OS_MAC) || defined(JST_OS_IOS)
        extensions.insert(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
#if defined(JST_OS_WINDOWS)
        extensions.insert(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
#if defined(JST_OS_ANDROID)
        extensions.insert(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
    }

    // Validation extensions.

    if (config.validationEnabled) {
        extensions.insert(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }

    return extensions;
}

std::set<std::string> Vulkan::getOptionalInstanceExtensions() {
    std::set<std::string> extensions;

#if defined(VK_KHR_portability_enumeration)
    extensions.insert(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

#if defined(VK_EXT_tooling_info)
    extensions.insert(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
#endif

    return extensions;
}

std::set<std::string> Vulkan::getRequiredValidationLayers() {
    std::set<std::string> layers;

    layers.insert("VK_LAYER_KHRONOS_validation");

    return layers;
}

std::set<std::string> Vulkan::getRequiredDeviceExtensions() {
    std::set<std::string> extensions;

    if (!config.headless) {
        extensions.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    return extensions;
}

std::set<std::string> Vulkan::getOptionalDeviceExtensions() {
    std::set<std::string> extensions;

#if defined(VK_KHR_external_memory_fd)
    extensions.insert(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    extensions.insert(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
#endif

#if defined(VK_KHR_portability_subset)
    extensions.insert(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    return extensions;
}

std::set<std::string> Vulkan::checkInstanceExtensionSupport(const std::set<std::string>& extensions) {
    uint32_t extensionCount;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> supportedExtensions;

    for (const auto& extension : availableExtensions) {
        if (extensions.contains(extension.extensionName)) {
            supportedExtensions.insert(extension.extensionName);
        }
    }

    return supportedExtensions;
}

std::set<std::string> Vulkan::checkValidationLayerSupport(const std::set<std::string>& layers) {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    std::set<std::string> supportedLayers;

    for (const auto& layer : availableLayers) {
        if (layers.contains(layer.layerName)) {
            supportedLayers.insert(layer.layerName);
        }
    }

    return supportedLayers;
}

std::set<std::string> Vulkan::checkDeviceExtensionSupport(const VkPhysicalDevice& device, const std::set<std::string>& extensions) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> supportedExtensions;

    for (const auto& extension : availableExtensions) {
        if (extensions.contains(extension.extensionName)) {
            supportedExtensions.insert(extension.extensionName);
        }
    }

    return supportedExtensions;
}

Vulkan::Vulkan(const Config& _config) : config(_config), cache({}) {
    // Gather instance extensions.

    std::vector<std::string> instanceExtensions;

    {
        const auto& requiredExtensions = getRequiredInstanceExtensions();
        const auto& optionalExtensions = getOptionalInstanceExtensions();
    
        JST_DEBUG("[VULKAN] Required instance extensions: {}", requiredExtensions);
        JST_DEBUG("[VULKAN] Optional instance extensions: {}", optionalExtensions);

        supportedInstanceExtensions.merge(checkInstanceExtensionSupport(requiredExtensions));

        std::set<std::string> unsupportedInstanceExtensions;

        for (const auto& extension : requiredExtensions) {
            if (!supportedInstanceExtensions.contains(extension)) {
                unsupportedInstanceExtensions.insert(extension);
            }
        }
        
        if (!unsupportedInstanceExtensions.empty()) {
            JST_FATAL("[VULKAN] Required instance extensions are not supported: {}.", unsupportedInstanceExtensions);
            JST_CHECK_THROW(Result::FATAL);
        }

        supportedInstanceExtensions.merge(checkInstanceExtensionSupport(optionalExtensions));

        for (const auto& extension : optionalExtensions) {
            if (!supportedInstanceExtensions.contains(extension)) {
                JST_WARN("[VULKAN] Optional instance extension '{}' is not supported.", extension);
            }
        }

        instanceExtensions.insert(instanceExtensions.end(), supportedInstanceExtensions.begin(), supportedInstanceExtensions.end());
    }

    // Gather validation layers.

    std::vector<std::string> validationLayers;

    {
        const auto& requiredValidationLayers = getRequiredValidationLayers();
        const auto& supportedValidationLayers = checkValidationLayerSupport(requiredValidationLayers);
        const auto& validationLayerCheck = requiredValidationLayers.size() == supportedValidationLayers.size();

        if (config.validationEnabled && !validationLayerCheck) {
            JST_WARN("[VULKAN] Couldn't find validation layers. Disabling Vulkan debug.");
            config.validationEnabled = false;
        }

        validationLayers.insert(validationLayers.end(), supportedValidationLayers.begin(), supportedValidationLayers.end());
    }

    // Create application.

    {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Jetstream";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
        appInfo.pEngineName = "Jetstream Vulkan Backend";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo instanceCreateInfo{};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &appInfo;
#if defined(VK_KHR_portability_enumeration)
        if (supportedInstanceExtensions.contains(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            JST_DEBUG("[VULKAN] Enabling portability enumeration.");
            instanceCreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        std::vector<const char*> vulkanInstanceExtensions(instanceExtensions.size());
        std::vector<const char*> vulkanValidationLayers(validationLayers.size());

        // Enable instance extensions.

        std::transform(instanceExtensions.begin(), instanceExtensions.end(), vulkanInstanceExtensions.begin(),
                       [](const std::string& str) { return str.c_str(); });

        instanceCreateInfo.enabledExtensionCount = static_cast<U32>(vulkanInstanceExtensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = vulkanInstanceExtensions.data();
        instanceCreateInfo.enabledLayerCount = 0;

        // Enable validation layers.

        if (config.validationEnabled) {
            std::transform(validationLayers.begin(), validationLayers.end(), vulkanValidationLayers.begin(),
                           [](const std::string& str) { return str.c_str(); });

            instanceCreateInfo.enabledLayerCount = static_cast<U32>(vulkanValidationLayers.size());
            instanceCreateInfo.ppEnabledLayerNames = vulkanValidationLayers.data();
        }

        // Create instance.

        JST_VK_CHECK_THROW(vkCreateInstance(&instanceCreateInfo, nullptr, &instance), [&]{
            JST_FATAL("[VULKAN] Couldn't create instance.");        
        });
    }

    // Setup validation layers.

    if (config.validationEnabled) {
        VkDebugReportCallbackCreateInfoEXT debugReportCreateInfo{};
        debugReportCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugReportCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debugReportCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;

        PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
            reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
        if (!vkCreateDebugReportCallbackEXT) {
            JST_FATAL("[VULKAN] Failed to create validation.");
            JST_CHECK_THROW(Result::FATAL);       
        }
        JST_VK_CHECK_THROW(vkCreateDebugReportCallbackEXT(instance, &debugReportCreateInfo, nullptr, &debugReportCallback), [&]{
            JST_FATAL("[VULKAN] Failed to create validation.");
        });
    }

    // Get physical device.

    {
        U32 physicalDeviceCount = 0;
        JST_VK_CHECK_THROW(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr), [&]{
            JST_FATAL("[VULKAN] Can't enumerate physical devices.");     
        });
        if (physicalDeviceCount == 0) {
            JST_FATAL("[VULKAN] No physical devices found.");
            JST_CHECK_THROW(Result::FATAL);
        }
        if (physicalDeviceCount <= config.deviceId) {
            JST_FATAL("[VULKAN] Can't find desired device ID ({}).", config.deviceId);
            JST_CHECK_THROW(Result::FATAL);
        }

        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

        std::vector<VkPhysicalDevice> validPhysicalDevices;
        for (const auto& candidatePhysicalDevice : physicalDevices) {
            const auto& requiredExtensions = getRequiredDeviceExtensions();
            const auto& supportedExtensions = checkDeviceExtensionSupport(candidatePhysicalDevice, requiredExtensions);
            const auto& extensionCheck = requiredExtensions.size() == supportedExtensions.size();

            const auto& queueFamilyIndices = FindQueueFamilies(candidatePhysicalDevice);
            const auto& queueFamilyCheck = queueFamilyIndices.isComplete();

            JST_DEBUG("[VULKAN] Candidate device - Extension check: {}, Queue family check: {}",
                      extensionCheck ? "OK" : "FAIL", queueFamilyCheck ? "OK" : "FAIL");

            if (extensionCheck && queueFamilyCheck) {
                validPhysicalDevices.push_back(candidatePhysicalDevice);
            }
        }
        if (validPhysicalDevices.size() == 0) {
            JST_FATAL("[VULKAN] No valid physical devices found.");
            JST_CHECK_THROW(Result::FATAL);
        }
        if (validPhysicalDevices.size() <= config.deviceId) {
            JST_FATAL("[VULKAN] Can't find desired device ID.");
            JST_CHECK_THROW(Result::FATAL);
        }
        physicalDevice = validPhysicalDevices[config.deviceId];

        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    }

    // Gather device extensions.

    std::vector<std::string> deviceExtensions;

    {
        const auto& requiredExtensions = getRequiredDeviceExtensions();
        const auto& optionalExtensions = getOptionalDeviceExtensions();

        JST_DEBUG("[VULKAN] Required device extensions: {}", requiredExtensions);
        JST_DEBUG("[VULKAN] Optional device extensions: {}", optionalExtensions);

        supportedDeviceExtensions.merge(checkDeviceExtensionSupport(physicalDevice, requiredExtensions));
        supportedDeviceExtensions.merge(checkDeviceExtensionSupport(physicalDevice, optionalExtensions));

        for (const auto& extension : optionalExtensions) {
            if (!supportedDeviceExtensions.contains(extension)) {
                JST_WARN("[VULKAN] Optional device extension '{}' is not supported.", extension);
            }
        }

        deviceExtensions.insert(deviceExtensions.end(), supportedDeviceExtensions.begin(), supportedDeviceExtensions.end());
    }

    // Populate information cache.

    cache.deviceName = properties.deviceName;
    cache.totalProcessorCount = std::thread::hardware_concurrency();
    cache.getThermalState.store(0);
    cache.lowPowerStatus.store(false);
    cache.telemetryProviderName = "Unavailable";

    {
        uint32_t major = VK_VERSION_MAJOR(properties.apiVersion);
        uint32_t minor = VK_VERSION_MINOR(properties.apiVersion);
        uint32_t patch = VK_VERSION_PATCH(properties.apiVersion);
        cache.apiVersion = jst::fmt::format("{}.{}.{}", major, minor, patch);
    }

    {
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                cache.physicalDeviceType = PhysicalDeviceType::INTEGRATED;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                cache.physicalDeviceType = PhysicalDeviceType::DISCRETE;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
            case VK_PHYSICAL_DEVICE_TYPE_OTHER:
                cache.physicalDeviceType = PhysicalDeviceType::OTHER;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM:
                cache.physicalDeviceType = PhysicalDeviceType::UNKNOWN;
                break;
            default:
                cache.physicalDeviceType = PhysicalDeviceType::UNKNOWN;
                break;
        }
    }

    {
        cache.hasUnifiedMemory = true;
        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
            VkMemoryHeap memoryHeap = memoryProperties.memoryHeaps[i];

            if (memoryHeap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                cache.physicalMemory += memoryHeap.size;
            }

            cache.hasUnifiedMemory &= (memoryHeap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
        }
    }

    cache.canImportDeviceMemory = supportedDeviceExtensions.contains(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    cache.canExportDeviceMemory = supportedDeviceExtensions.contains(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    cache.canImportHostMemory = supportedDeviceExtensions.contains(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);

    initializeTelemetry();

    // Create logical device.

    {
        QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicFamily.value(),
            indices.computeFamily.value(),
            indices.presentFamily.value(),
        };

        float queuePriority = 0.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        createInfo.queueCreateInfoCount = static_cast<U32>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;

        std::vector<const char*> vulkanDeviceExtensions(deviceExtensions.size());
        std::vector<const char*> vulkanValidationLayers(validationLayers.size());

        std::transform(deviceExtensions.begin(), deviceExtensions.end(), vulkanDeviceExtensions.begin(),
                       [](const std::string& str) { return str.c_str(); });

        createInfo.enabledExtensionCount = static_cast<U32>(vulkanDeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = vulkanDeviceExtensions.data();
        createInfo.enabledLayerCount = 0;

        if (config.validationEnabled) {
            std::transform(validationLayers.begin(), validationLayers.end(), vulkanValidationLayers.begin(),
                           [](const std::string& str) { return str.c_str(); });

            createInfo.enabledLayerCount = static_cast<U32>(vulkanValidationLayers.size());
            createInfo.ppEnabledLayerNames = vulkanValidationLayers.data();
        }

        JST_VK_CHECK_THROW(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), [&]{
            JST_FATAL("[VULKAN] Can't create logical device.");     
        });

        vkGetDeviceQueue(device, indices.graphicFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.computeFamily.value(), 0, &computeQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    }

    // Validate multisampling level from configuration.

    {
        VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts &
                                    properties.limits.framebufferDepthSampleCounts;

        const U64 maxSamples = counts & VK_SAMPLE_COUNT_64_BIT ? 64 :
                               counts & VK_SAMPLE_COUNT_32_BIT ? 32 :
                               counts & VK_SAMPLE_COUNT_16_BIT ? 16 :
                               counts & VK_SAMPLE_COUNT_8_BIT  ?  8 :
                               counts & VK_SAMPLE_COUNT_4_BIT  ?  4 :
                               counts & VK_SAMPLE_COUNT_2_BIT  ?  2 : 1;

        if (config.multisampling > maxSamples) {
            JST_WARN("[VULKAN] Requested multisampling level ({}) is not supported. Using {} instead.", config.multisampling, maxSamples);
            config.multisampling = maxSamples;
        }
    }

    // Create descriptor pool.

    {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptorPoolInfo.maxSets = 1000;
        descriptorPoolInfo.poolSizeCount = std::size(poolSizes);
        descriptorPoolInfo.pPoolSizes = poolSizes;

        JST_VK_CHECK_THROW(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool), [&]{
            JST_FATAL("[VULKAN] Can't create descriptor pool.")
        });
    }

    // Create staging buffer.

    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = config.stagingBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | 
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        JST_VK_CHECK_THROW(vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer), [&]{
            JST_FATAL("[VULKAN] Failed to create staging buffer.");
        });

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = Backend::FindMemoryType(physicalDevice,
                                                            memRequirements.memoryTypeBits,
                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        JST_VK_CHECK_THROW(vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory), [&]{
            JST_FATAL("[VULKAN] Failed to allocate staging buffer memory.");
        });

        JST_VK_CHECK_THROW(vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0), [&]{
            JST_FATAL("[VULKAN] Failed to bind memory to staging buffer.");
        });
        
        JST_VK_CHECK_THROW(vkMapMemory(device, stagingBufferMemory, 0, config.stagingBufferSize, 0, &stagingBufferMappedMemory), [&]{
            JST_FATAL("[VULKAN] Failed to map staging buffer memory.");        
        });
    }

    // Create default command pool.

    {
        Backend::QueueFamilyIndices indices = Backend::FindQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = indices.computeFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                         VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        JST_VK_CHECK_THROW(vkCreateCommandPool(device, &poolInfo, nullptr, &defaultCommandPool), [&]{
            JST_FATAL("[VULKAN] Failed to create default command pool.");
        });
    }

    // Create default command buffer.

    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = defaultCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        JST_VK_CHECK_THROW(vkAllocateCommandBuffers(device, &allocInfo, &defaultCommandBuffer), [&]{
            JST_ERROR("[VULKAN] Failed to create default command buffer.");
        });
    }

    // Create default fence.

    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        JST_VK_CHECK_THROW(vkCreateFence(device, &fenceInfo, nullptr, &defaultFence), [&]{
            JST_ERROR("[VULKAN] Failed to create default fence.");            
        });
    }

    // Signal device is available.

    _isAvailable = true;

    // Print device information.

    JST_INFO("-----------------------------------------------------");
    JST_INFO("Jetstream Heterogeneous Backend [VULKAN]")
    JST_INFO("-----------------------------------------------------");
    JST_INFO("Device Name:      {}", getDeviceName());
    JST_INFO("Device Type:      {}", getPhysicalDeviceType());
    JST_INFO("API Version:      {}", getApiVersion());
    JST_INFO("Unified Memory:   {}", hasUnifiedMemory() ? "YES" : "NO");
    JST_INFO("Processor Count:  {}", getTotalProcessorCount());
    JST_INFO("Device Memory:    {:.2f} GB", static_cast<F32>(getPhysicalMemory()) / (1024*1024*1024));
    JST_INFO("Staging Buffer:   {:.2f} MB", static_cast<F32>(config.stagingBufferSize) / JST_MB);
    JST_INFO("Interoperability:");
    JST_INFO("  - Can Import Device Memory: {}", canImportDeviceMemory() ? "YES" : "NO");
    JST_INFO("  - Can Export Device Memory: {}", canExportDeviceMemory() ? "YES" : "NO");
    JST_INFO("  - Can Export Host Memory:   {}", canImportHostMemory() ? "YES" : "NO");
    JST_INFO("-----------------------------------------------------");
}

void Vulkan::initializeTelemetry() {
    cache.telemetryProviderType = Telemetry::Provider::NONE;
    cache.telemetryProviderName = "Unavailable";
    cache.telemetryHandle = nullptr;
    cache.telemetryContext = nullptr;
    cache.telemetryLibraryInitialized = false;
    cache.toolingInfoFn = nullptr;

    bool telemetryReady = false;

#if JST_BACKEND_HAS_NVML
    if (properties.vendorID == 0x10DE) {
        telemetryReady = setupNvmlTelemetry();
    }
#endif

    if (!telemetryReady && supportedInstanceExtensions.contains(VK_EXT_TOOLING_INFO_EXTENSION_NAME)) {
        auto toolingFn = reinterpret_cast<PFN_vkGetPhysicalDeviceToolPropertiesEXT>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceToolPropertiesEXT"));
        if (toolingFn != nullptr) {
            cache.telemetryProviderType = Telemetry::Provider::TOOLING_INFO;
            cache.telemetryProviderName = "VK_EXT_tooling_info";
            cache.telemetryHandle = physicalDevice;
            cache.telemetryContext = nullptr;
            cache.toolingInfoFn = toolingFn;
            telemetryReady = true;
        }
    }

    if (!telemetryReady) {
        JST_WARN("[VULKAN] Telemetry data is unavailable for '{}'.", cache.deviceName);
        return;
    }

    pollTelemetry();
    startTelemetryPolling();
    JST_INFO("[VULKAN] Telemetry provider: {}", cache.telemetryProviderName);
}

bool Vulkan::setupNvmlTelemetry() {
#if JST_BACKEND_HAS_NVML
    const nvmlReturn_t initStatus = nvmlInit_v2();
    if (initStatus != NVML_SUCCESS) {
        JST_ERROR("[VULKAN] NVML initialization failed: {}", nvmlErrorString(initStatus));
        return false;
    }

    nvmlDevice_t deviceHandle{};
    const auto nvmlStatus = nvmlDeviceGetHandleByIndex(static_cast<unsigned int>(config.deviceId), &deviceHandle);
    if (nvmlStatus != NVML_SUCCESS) {
        JST_ERROR("[VULKAN] NVML cannot access device {}: {}", config.deviceId, nvmlErrorString(nvmlStatus));
        nvmlShutdown();
        return false;
    }

    cache.telemetryProviderType = Telemetry::Provider::NVML;
    cache.telemetryProviderName = "NVML";
    cache.telemetryHandle = reinterpret_cast<void*>(deviceHandle);
    cache.telemetryLibraryInitialized = true;
    cache.toolingInfoFn = nullptr;

    return true;
#else
    return false;
#endif
}

void Vulkan::startTelemetryPolling() {
    if (telemetryRuntime.running.load()) {
        return;
    }

    telemetryRuntime.running.store(true);
    telemetryRuntime.providerErrorLogged.store(false);
    telemetryRuntime.worker = std::thread([this] {
        while (telemetryRuntime.running.load()) {
            pollTelemetry();
            std::this_thread::sleep_for(telemetryRuntime.interval);
        }
    });
}

void Vulkan::stopTelemetryPolling() {
    const bool wasRunning = telemetryRuntime.running.exchange(false);
    if (wasRunning && telemetryRuntime.worker.joinable()) {
        telemetryRuntime.worker.join();
    }
}

void Vulkan::pollTelemetry() {
    switch (cache.telemetryProviderType) {
        case Telemetry::Provider::NVML:
            updateTelemetryFromNvml();
            break;
        case Telemetry::Provider::TOOLING_INFO:
            updateTelemetryFromToolingInfo();
            break;
        default:
            break;
    }
}

void Vulkan::updateTelemetryFromNvml() {
#if JST_BACKEND_HAS_NVML
    auto deviceHandle = reinterpret_cast<nvmlDevice_t>(cache.telemetryHandle);
    if (deviceHandle == nullptr) {
        return;
    }

    nvmlPstates_t powerState{};
    const nvmlReturn_t powerStateStatus = nvmlDeviceGetPowerState(deviceHandle, &powerState);
    if (powerStateStatus == NVML_SUCCESS) {
        cache.lowPowerStatus.store(Telemetry::IsLowPowerFromPState(static_cast<U32>(powerState)));
    } else if (!telemetryRuntime.providerErrorLogged.load()) {
        JST_WARN("[VULKAN] Failed to query NVML power state: {}", nvmlErrorString(powerStateStatus));
        telemetryRuntime.providerErrorLogged.store(true);
    }

    unsigned int temperature = 0;
    const nvmlReturn_t temperatureStatus = nvmlDeviceGetTemperature(deviceHandle, NVML_TEMPERATURE_GPU, &temperature);
    if (temperatureStatus == NVML_SUCCESS) {
        cache.getThermalState.store(Telemetry::ThermalBucketFromCelsius(temperature));
    } else if (!telemetryRuntime.providerErrorLogged.load()) {
        JST_WARN("[VULKAN] Failed to query NVML temperature: {}", nvmlErrorString(temperatureStatus));
        telemetryRuntime.providerErrorLogged.store(true);
    }

    unsigned int powerUsageMw = 0;
    unsigned int powerBudgetMw = 0;
    const auto usageStatus = nvmlDeviceGetPowerUsage(deviceHandle, &powerUsageMw);
    const auto budgetStatus = nvmlDeviceGetEnforcedPowerLimit(deviceHandle, &powerBudgetMw);
    if (usageStatus == NVML_SUCCESS && budgetStatus == NVML_SUCCESS) {
        const bool lowPower = Telemetry::IsLowPowerFromPowerBudget(powerUsageMw, powerBudgetMw);
        cache.lowPowerStatus.store(lowPower);
    } else if (!telemetryRuntime.providerErrorLogged.load()) {
        const auto failedStatus = usageStatus != NVML_SUCCESS ? usageStatus : budgetStatus;
        JST_WARN("[VULKAN] Failed to query NVML power budget: {}", nvmlErrorString(failedStatus));
        telemetryRuntime.providerErrorLogged.store(true);
    }
#endif
}

void Vulkan::updateTelemetryFromToolingInfo() {
    if (cache.toolingInfoFn == nullptr) {
        return;
    }

    uint32_t toolCount = 0;
    const VkResult countResult = cache.toolingInfoFn(physicalDevice, &toolCount, nullptr);
    if (countResult != VK_SUCCESS) {
        if (!telemetryRuntime.providerErrorLogged.load()) {
            JST_WARN("[VULKAN] vkGetPhysicalDeviceToolPropertiesEXT count failed: {}", countResult);
            telemetryRuntime.providerErrorLogged.store(true);
        }
        return;
    }

    std::vector<VkPhysicalDeviceToolPropertiesEXT> tools(toolCount);
    for (auto& tool : tools) {
        tool.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES_EXT;
        tool.pNext = nullptr;
    }

    const VkResult toolsResult = cache.toolingInfoFn(physicalDevice, &toolCount, tools.data());
    if (toolsResult != VK_SUCCESS) {
        if (!telemetryRuntime.providerErrorLogged.load()) {
            JST_WARN("[VULKAN] vkGetPhysicalDeviceToolPropertiesEXT failed: {}", toolsResult);
            telemetryRuntime.providerErrorLogged.store(true);
        }
        return;
    }

    bool monitoringToolPresent = false;
    for (const auto& tool : tools) {
        if (tool.purposes & VK_TOOL_PURPOSE_MONITORING_BIT_EXT) {
            monitoringToolPresent = true;
            break;
        }
    }

    cache.lowPowerStatus.store(!monitoringToolPresent);
    cache.getThermalState.store(monitoringToolPresent ? 1 : 0);
}

Vulkan::~Vulkan() {
    stopTelemetryPolling();

#if JST_BACKEND_HAS_NVML
    if (cache.telemetryProviderType == Telemetry::Provider::NVML && cache.telemetryLibraryInitialized) {
        nvmlShutdown();
        cache.telemetryLibraryInitialized = false;
    }
#endif

    vkDestroyFence(device, defaultFence, nullptr);
    vkFreeCommandBuffers(device, defaultCommandPool, 1, &defaultCommandBuffer);
    vkDestroyCommandPool(device, defaultCommandPool, nullptr);
    vkUnmapMemory(device, stagingBufferMemory);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

    if (debugReportCallback != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
                reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
        vkDestroyDebugReportCallbackEXT(instance, debugReportCallback, nullptr);
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}

VkSampleCountFlagBits Vulkan::getMultisampling() const {
    switch (config.multisampling) {
        case  1: return VK_SAMPLE_COUNT_1_BIT;
        case  2: return VK_SAMPLE_COUNT_2_BIT;
        case  4: return VK_SAMPLE_COUNT_4_BIT;
        case  8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

bool Vulkan::isAvailable() const {
    return _isAvailable;
}

std::string Vulkan::getDeviceName() const {
    return cache.deviceName;
}

std::string Vulkan::getApiVersion() const {
    return cache.apiVersion;       
}

PhysicalDeviceType Vulkan::getPhysicalDeviceType() const {
    return cache.physicalDeviceType; 
}

bool Vulkan::hasUnifiedMemory() const {
    return cache.hasUnifiedMemory;
}

bool Vulkan::canExportDeviceMemory() const {
    return cache.canExportDeviceMemory;
}

bool Vulkan::canImportDeviceMemory() const {
    return cache.canImportDeviceMemory;
}

bool Vulkan::canImportHostMemory() const {
    return cache.canImportHostMemory;
}

U64 Vulkan::getPhysicalMemory() const {
    return cache.physicalMemory;
}

U64 Vulkan::getTotalProcessorCount() const {
    return cache.totalProcessorCount;
}

bool Vulkan::getLowPowerStatus() const {
    return cache.lowPowerStatus.load();
}

U64 Vulkan::getThermalState() const {
    return cache.getThermalState.load();
}

}  // namespace Jetstream::Backend
