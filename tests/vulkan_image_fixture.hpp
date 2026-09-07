#pragma once
// GPU-only same-command-buffer dependency test: upload -> NR -> readback.
// No cross-API sharing, host-signaled events, presentation or game interception.
#define NR_IMAGE_FUNCTIONS(X) \
 X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
 X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) X(vkDestroyBuffer) \
 X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
 X(vkCmdPipelineBarrier) X(vkCmdClearColorImage) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer)
struct VulkanImageFixture
{
    static constexpr unsigned width = 1920, height = 1080;
    static constexpr VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 8;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory = {};
    VkImage images[4] = {};
    VkImageView views[4] = {};
    VkDeviceMemory allocations[4] = {};
    VkBuffer buffers[2] = {}; // upload, readback
    VkDeviceMemory buffer_memory[2] = {};
    NVSDK_NGX_Resource_VK resources[4] = {};
#define DECLARE_FUNCTION(name) PFN_##name name = nullptr;
    NR_IMAGE_FUNCTIONS(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION
    unsigned memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
    {
        for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("image fixture memory type unavailable");
    }
    void create(VkInstance instance, VkPhysicalDevice physical, VkDevice owner,
                PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa)
    {
#define LOAD_FUNCTION(name) name = required<PFN_##name>(gdpa(owner, #name), #name);
        NR_IMAGE_FUNCTIONS(LOAD_FUNCTION)
#undef LOAD_FUNCTION
        device = owner;
        const auto properties = required<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties"), "memory properties");
        const auto formats = required<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(instance, "vkGetPhysicalDeviceFormatProperties"), "format properties");
        properties(physical, &memory);
        const VkFormat types[] = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
        for (unsigned i = 0; i < 4; ++i)
        {
            VkFormatProperties format = {}; formats(physical, types[i], &format);
            const auto needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            if ((format.optimalTilingFeatures & needed) != needed) throw std::runtime_error("image fixture format unsupported");
            VkImageCreateInfo info = {}; info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType = VK_IMAGE_TYPE_2D; info.format = types[i]; info.extent = { width, height, 1 };
            info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            check(vkCreateImage(device, &info, nullptr, &images[i]), "create fixture image");
            VkMemoryRequirements requirement = {}; vkGetImageMemoryRequirements(device, images[i], &requirement);
            VkMemoryAllocateInfo allocation = {}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirement.size; allocation.memoryTypeIndex = memory_type(requirement.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &allocation, nullptr, &allocations[i]), "allocate image memory");
            check(vkBindImageMemory(device, images[i], allocations[i], 0), "bind image memory");
            VkImageViewCreateInfo view = {}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = types[i];
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            check(vkCreateImageView(device, &view, nullptr, &views[i]), "create fixture view");
            resources[i].Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW; resources[i].ReadWrite = i == 3;
            resources[i].Resource.ImageViewInfo = { views[i], images[i], view.subresourceRange, types[i], width, height };
        }
        for (unsigned i = 0; i < 2; ++i)
        {
            VkBufferCreateInfo info = {}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = bytes; info.usage = i ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            check(vkCreateBuffer(device, &info, nullptr, &buffers[i]), "create staging buffer");
            VkMemoryRequirements requirement = {}; vkGetBufferMemoryRequirements(device, buffers[i], &requirement);
            VkMemoryAllocateInfo allocation = {}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirement.size; allocation.memoryTypeIndex = memory_type(requirement.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkAllocateMemory(device, &allocation, nullptr, &buffer_memory[i]), "allocate staging memory");
            check(vkBindBufferMemory(device, buffers[i], buffer_memory[i], 0), "bind staging memory");
        }
        void *mapped = nullptr; check(vkMapMemory(device, buffer_memory[0], 0, bytes, 0, &mapped), "map upload");
        auto *pixels = static_cast<unsigned short *>(mapped);
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
        {
            const auto offset = (static_cast<size_t>(y) * width + x) * 4;
            const unsigned checker = ((x / 32 + y / 32) & 1) * 0x400;
            pixels[offset] = static_cast<unsigned short>(0x3000 + x * 2048 / width + checker);
            pixels[offset + 1] = static_cast<unsigned short>(0x3000 + y * 2048 / height);
            pixels[offset + 2] = static_cast<unsigned short>(0x3400 + checker);
            pixels[offset + 3] = 0x3c00;
        }
        vkUnmapMemory(device, buffer_memory[0]);
    }
    void record_inputs(VkCommandBuffer commands, NVSDK_NGX_Parameter *parameters, float intensity)
    {
        VkImageMemoryBarrier barriers[4] = {};
        for (unsigned i = 0; i < 4; ++i)
        {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = images[i]; barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers);
        VkBufferImageCopy copy = {}; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; copy.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(commands, buffers[0], images[0], VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        for (unsigned i = 1; i < 4; ++i)
        {
            VkClearColorValue clear = {}; clear.float32[0] = i == 2 ? 0.5f : (i == 3 ? -1.0f : 0.0f);
            clear.float32[3] = 1.0f;
            vkCmdClearColorImage(commands, images[i], VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &barriers[i].subresourceRange);
        }
        for (auto &barrier : barriers)
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 4, barriers);
        const char *names[] = { "Color", "MVec", "Depth", "Output" };
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto prefix = std::string("DLSSNR.") + names[i];
            parameters->Set(prefix.c_str(), static_cast<void *>(&resources[i]));
            parameters->Set((prefix + ".Subrect.BaseX").c_str(), 0u); parameters->Set((prefix + ".Subrect.BaseY").c_str(), 0u);
            parameters->Set((prefix + ".Subrect.Width").c_str(), width); parameters->Set((prefix + ".Subrect.Height").c_str(), height);
        }
        parameters->Set("DLSSNR.UI", static_cast<void *>(nullptr)); parameters->Set("DLSSNR.UIAlpha", static_cast<void *>(nullptr));
        parameters->Set("DLSSNR.Enabled", 1); parameters->Set("DLSSNR.Reset", 1);
        parameters->Set("DLSSNR.DepthInverted", 0); parameters->Set("DLSSNR.UICorrection", 0);
        parameters->Set("DLSSNR.Intensity", intensity); parameters->Set("DLSSNR.Style", 2u);
        for (const char *key : { "DLSSNR.LocalToneStrength", "DLSSNR.LocalStructureStrength", "DLSSNR.GlobalToneStrength", "DLSSNR.SkinStructureStrength" }) parameters->Set(key, 1.0f);
        parameters->Set("DLSSNR.UseAutoMask", 0);
        for (const char *key : { "Jitter.Offset.X", "Jitter.Offset.Y", "DLSSNR.JitterOffsetX", "DLSSNR.JitterOffsetY" }) parameters->Set(key, 0.0f);
        parameters->Set("DLSSNR.MVecScaleX", static_cast<float>(width)); parameters->Set("DLSSNR.MVecScaleY", static_cast<float>(height));
    }
    void record_readback(VkCommandBuffer commands)
    {
        VkMemoryBarrier barrier = {}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        VkBufferImageCopy copy = {}; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; copy.imageExtent = { width, height, 1 };
        vkCmdCopyImageToBuffer(commands, images[3], VK_IMAGE_LAYOUT_GENERAL, buffers[1], 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    bool capture() // caller has observed actual submission fence completion
    {
        void *mapped = nullptr; check(vkMapMemory(device, buffer_memory[1], 0, bytes, 0, &mapped), "map readback");
        const auto *pixels = static_cast<unsigned short *>(mapped);
        size_t nonfinite = 0, untouched = 0;
        for (size_t i = 0; i < bytes / 2; ++i) nonfinite += (pixels[i] & 0x7c00) == 0x7c00;
        for (size_t i = 0; i < bytes / 2; i += 4) untouched += pixels[i] == 0xbc00 && pixels[i + 1] == 0 && pixels[i + 2] == 0;
        std::printf("Vulkan GPU readback: nonfinite=%zu sentinel-pixels=%zu\n", nonfinite, untouched);
        FILE *file = nullptr; const unsigned header[] = { 0x314f524e, width, height, 10 };
        bool saved = !fopen_s(&file, "vulkan-output.bin", "wb") && file;
        if (saved) { saved = fwrite(header, 1, sizeof(header), file) == sizeof(header) && fwrite(mapped, 1, static_cast<size_t>(bytes), file) == bytes; saved = fclose(file) == 0 && saved; }
        vkUnmapMemory(device, buffer_memory[1]);
        return saved && !nonfinite && !untouched;
    }
    void destroy() // no submitted work remains; feature/snippet already released
    {
        if (!device) return;
        for (unsigned i = 0; i < 4; ++i)
        { if (views[i]) vkDestroyImageView(device, views[i], nullptr); if (images[i]) vkDestroyImage(device, images[i], nullptr); if (allocations[i]) vkFreeMemory(device, allocations[i], nullptr); }
        for (unsigned i = 0; i < 2; ++i)
        { if (buffers[i]) vkDestroyBuffer(device, buffers[i], nullptr); if (buffer_memory[i]) vkFreeMemory(device, buffer_memory[i], nullptr); }
        device = VK_NULL_HANDLE;
    }
};
#undef NR_IMAGE_FUNCTIONS
