#include "androidframepacing.h"
#include "vulkanobjects.h"
#include <SDL.h>
#include <SDL_system.h>
#include <android/native_window_jni.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Swappy 2.1.3 recycles its internal signal semaphores when its GPU fence
// completes, which does not prove that presentation consumed them. Consume
// them in an ordered queue submission and give WSI a semaphore per image.
// The engine waits on image acquisition before rendering/re-signaling that
// image's semaphore. All swapchain/present operations run on the render thread.
#include <map>
#include <dlfcn.h>
extern "C" void* OpenRuneVulkanSymbol(const char* name);
namespace {
struct PresentBridge {
    VkDevice device;
    std::vector<VkSemaphore> semaphores;
};
std::map<VkSwapchainKHR, PresentBridge> bridges;
int targetFps=120;
bool runtimePacing=true;

VKAPI_ATTR VkResult VKAPI_CALL PacedPresent(VkQueue queue, const VkPresentInfoKHR* info)
{
    if (info->swapchainCount != 1) return VK_ERROR_INITIALIZATION_FAILED;
    auto it = bridges.find(info->pSwapchains[0]);
    if (it == bridges.end()) return VK_ERROR_INITIALIZATION_FAILED;
    auto& bridge = it->second;
    uint32_t index = info->pImageIndices[0];
    if (index >= bridge.semaphores.size()) return VK_ERROR_INITIALIZATION_FAILED;
    VkSemaphore ready = bridge.semaphores[index];
    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = info->waitSemaphoreCount;
    submit.pWaitSemaphores = info->pWaitSemaphores;
    submit.pWaitDstStageMask = stages.data();
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &ready;
    VkResult result = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) return result;
    VkPresentInfoKHR present = *info; // Preserve Swappy's desired presentation times.
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &ready;
    return vkQueuePresentKHR(queue, &present);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL PacingDeviceProc(VkDevice device, const char* name)
{
    if (std::strcmp(name, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(PacedPresent);
    return vkGetDeviceProcAddr(device, name);
}
bool ProviderInit()
{
    return OpenRuneVulkanSymbol("vkGetInstanceProcAddr") != nullptr;
}
void* ProviderProc(const char* name)
{
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<void*>(PacingDeviceProc);
    return OpenRuneVulkanSymbol(name);
}
void ProviderClose()
{
    // Shared loader lives until process exit; SDL and Volk still reference it.
}
const SwappyVkFunctionProvider provider = { ProviderInit, ProviderProc, ProviderClose };
}

bool AndroidFramePacingActive() { return runtimePacing; }
void AndroidFramePacingConfigure(int fps, int enabled)
{
    if (enabled>=0 && runtimePacing!=(enabled!=0)) {
        // Drain queued work before changing which presentation path owns it.
        for (const auto& pair : bridges) vkDeviceWaitIdle(pair.second.device);
        runtimePacing=enabled!=0;
    }
    if (fps>=30 && fps<=1000) targetFps=fps;
    for (const auto& pair : bridges)
        SwappyVk_setSwapIntervalNS(pair.second.device,pair.first,1000000000ULL/targetFps);
    std::fprintf(stderr,"RUNE_MENU pacing=%d target_fps=%d\n",runtimePacing,targetFps);
}

void AndroidFramePacingDestroy(VulkanDevice* device, VkSwapchainKHR swapchain)
{
    SwappyVk_destroySwapchain(device->device, swapchain);
    auto it = bridges.find(swapchain);
    if (it != bridges.end()) {
        for (auto semaphore : it->second.semaphores)
            vkDestroySemaphore(device->device, semaphore, nullptr);
        bridges.erase(it);
    }
}

bool AndroidFramePacingRequested()
{
    const char* value = std::getenv("SURREAL_ANDROID_FRAME_PACING");
    return value && std::strcmp(value, "1") == 0;
}

void AndroidFramePacingExtensions(VulkanDevice* device)
{
    if (!AndroidFramePacingRequested()) return;
    static const bool configured = [] { SwappyVk_setFunctionProvider(&provider); return true; }();
    (void)configured;
    uint32_t count = 0;
    device->CheckVulkanError(vkEnumerateDeviceExtensionProperties(device->PhysicalDevice.Device, nullptr, &count, nullptr), "Enumerating pacing extensions");
    std::vector<VkExtensionProperties> available(count);
    device->CheckVulkanError(vkEnumerateDeviceExtensionProperties(device->PhysicalDevice.Device, nullptr, &count, available.data()), "Reading pacing extensions");
    uint32_t required = 0;
    SwappyVk_determineDeviceExtensions(device->PhysicalDevice.Device, count, available.data(), &required, nullptr);
    std::vector<std::vector<char>> names(required, std::vector<char>(VK_MAX_EXTENSION_NAME_SIZE + 1));
    std::vector<char*> pointers;
    for (auto& name : names) pointers.push_back(name.data());
    if (required) SwappyVk_determineDeviceExtensions(device->PhysicalDevice.Device, count, available.data(), &required, pointers.data());
    for (auto name : pointers) {
        device->EnabledDeviceExtensions.insert(name);
        std::fprintf(stderr, "ANDROID_PACING extension=%s\n", name);
    }
}

bool AndroidFramePacingInit(VulkanDevice* device, VkSwapchainKHR swapchain)
{
    if (!AndroidFramePacingRequested()) return false;
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        std::fprintf(stderr, "ANDROID_PACING active=0 reason=no_activity\n");
        return false;
    }
    uint32_t imageCount = 0;
    device->CheckVulkanError(vkGetSwapchainImagesKHR(device->device, swapchain, &imageCount, nullptr), "Counting pacing images");
    auto& bridge = bridges[swapchain];
    bridge.device = device->device;
    bridge.semaphores.resize(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& semaphore : bridge.semaphores)
        device->CheckVulkanError(vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore), "Creating pacing semaphore");
    SwappyVk_setQueueFamilyIndex(device->device, device->PresentQueue, device->PresentFamily);
    uint64_t refresh = 0;
    bool initialized = SwappyVk_initAndGetRefreshCycleDuration(env, activity, device->PhysicalDevice.Device, device->device, swapchain, &refresh);
    if (initialized) {
        // These APIs configure existing instances; calling before init is a no-op.
        SwappyVk_setAutoSwapInterval(false);
        SwappyVk_setAutoPipelineMode(true);
        // SDL owns the Surface used to create this swapchain. Release our JNI and
        // native-window references after Swappy has retained its own reference.
        jclass cls = env->GetObjectClass(activity);
        jmethodID getSurface = env->GetStaticMethodID(cls, "getNativeSurface", "()Landroid/view/Surface;");
        jobject surface = getSurface ? env->CallStaticObjectMethod(cls, getSurface) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); surface = nullptr; }
        if (surface) {
            ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
            if (window) {
                SwappyVk_setWindow(device->device, swapchain, window);
                ANativeWindow_release(window);
            }
            env->DeleteLocalRef(surface);
        }
        env->DeleteLocalRef(cls);
        SwappyVk_setSwapIntervalNS(device->device, swapchain, 1000000000ULL/targetFps);
    }
    env->DeleteLocalRef(activity);
    bool enabled = false;
    if (initialized) SwappyVk_isEnabled(swapchain, &enabled);
    std::fprintf(stderr, "ANDROID_PACING active=%d initialized=%d target_fps=%d refresh_ns=%llu library=androidx-2.1.3\n", enabled, initialized, targetFps, (unsigned long long)refresh);
    if (!initialized) AndroidFramePacingDestroy(device, swapchain);
    return initialized;
}
