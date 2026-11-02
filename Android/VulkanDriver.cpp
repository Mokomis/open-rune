// App-local Vulkan loader. SDL, Volk and Swappy must share this loader instance.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#ifdef OPENRUNE_ADRENOTOOLS
#include <adrenotools/driver.h>
#endif

extern "C" __attribute__((visibility("default"))) void* OpenRuneVulkanSymbol(const char* name)
{
    static std::once_flag once;
    static void* loader = nullptr; // Process lifetime, including all surface/pacing users.
    std::call_once(once, [] {
        const char* choice = std::getenv("OPENRUNE_VULKAN_DRIVER");
        const char* libs = std::getenv("OPENRUNE_NATIVE_LIB_DIR");
        bool turnip = choice && std::strcmp(choice, "turnip") == 0;
#ifdef OPENRUNE_ADRENOTOOLS
        if (turnip && libs) {
            loader = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL,
                ADRENOTOOLS_DRIVER_CUSTOM, nullptr, libs, libs,
                "libvulkan_freedreno.so", nullptr, nullptr);
        }
#endif
        if (!turnip) loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        // Never silently measure the stock driver when Turnip was requested.
        std::fprintf(stderr, "RUNE_VULKAN_LOADER requested=%s loaded=%d\n",
            turnip ? "turnip" : "system", loader != nullptr);
        if (!loader) {
            const char* error = dlerror();
            std::fprintf(stderr, "RUNE_VULKAN_LOADER_ERROR %s\n",
                error ? error : "Selected driver unavailable in this build");
        }
    });
    return loader ? dlsym(loader, name) : nullptr;
}

// SDL loads this shim through SDL_VULKAN_LIBRARY, preserving its native WSI path.
extern "C" __attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(OpenRuneVulkanSymbol("vkGetInstanceProcAddr"));
    return proc ? proc(instance, name) : nullptr;
}
