#pragma once

#include <surrealgpu/vulkandevice.h>
#include <cstring>

// Performance defaults require matched native-resolution, 4x-AA comparisons.
// The explicit renderer flags remain available for comparisons and fallback.
inline bool UsesQualifiedShaderDefaults(const VulkanDevice& device)
{
#if defined(__APPLE__)
    return true;
#elif defined(__ANDROID__)
    // Qualified on OPD2515: identical captures and repeatable frame-time gains.
    // Do not extend the result to other Adrenos, Mali, or the optional Turnip driver.
    if (std::strcmp(device.PhysicalDevice.Properties.Properties.deviceName, "Adreno (TM) 829") != 0)
        return false;
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &driver;
    vkGetPhysicalDeviceProperties2(device.PhysicalDevice.Device, &properties);
    return driver.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY;
#else
    return false;
#endif
}
