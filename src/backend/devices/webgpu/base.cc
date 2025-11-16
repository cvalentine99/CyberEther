#include "jetstream/logger.hh"

#include "jetstream/backend/devices/webgpu/base.hh"

EM_JS(int, jetstream_webgpu_query_low_power_hint, (), {
    if (typeof navigator !== 'undefined') {
        if (navigator.deviceMemory !== undefined) {
            return navigator.deviceMemory <= 4 ? 1 : 0;
        }
        if (navigator.hardwareConcurrency !== undefined) {
            return navigator.hardwareConcurrency <= 4 ? 1 : 0;
        }
    }
    return -1;
});

EM_JS(int, jetstream_webgpu_query_thermal_bucket, (), {
    if (typeof performance !== 'undefined' && performance.memory) {
        var limit = performance.memory.jsHeapSizeLimit || 0;
        if (limit > 0) {
            var ratio = performance.memory.usedJSHeapSize / limit;
            if (ratio > 0.9) return 3;
            if (ratio > 0.75) return 2;
            if (ratio > 0.5) return 1;
            return 0;
        }
    }
    if (typeof navigator !== 'undefined' && navigator.deviceMemory !== undefined) {
        var memoryHint = navigator.deviceMemory;
        if (memoryHint <= 4) return 2;
        if (memoryHint <= 8) return 1;
        return 0;
    }
    return -1;
});

namespace Jetstream::Backend {

static void WebGPUErrorCallback(WGPUErrorType error_type, const char* message, void*) {
    const char* error_type_lbl = "";
    switch (error_type) {
        case WGPUErrorType_Validation:  error_type_lbl = "Validation";    break;
        case WGPUErrorType_OutOfMemory: error_type_lbl = "Out of memory"; break;
        case WGPUErrorType_Unknown:     error_type_lbl = "Unknown";       break;
        case WGPUErrorType_DeviceLost:  error_type_lbl = "Device lost";   break;
        default:                        error_type_lbl = "Unknown";
    }
    JST_FATAL("[WebGPU] {} error: {}", error_type_lbl, message);
    JST_CHECK_THROW(Result::FATAL);
}

EM_JS(WGPUAdapter, wgpuInstanceRequestAdapterSync, (), {
    return Module["preinitializedWebGPUAdapter"];
});

WebGPU::WebGPU(const Config& _config) : config(_config), cache({}) {
    // Create application.

    adapter = wgpu::Adapter::Acquire(wgpuInstanceRequestAdapterSync());
    device = wgpu::Device::Acquire(emscripten_webgpu_get_device());

    device.SetUncapturedErrorCallback(&WebGPUErrorCallback, nullptr);

    cache.lowPowerStatus.store(false);
    cache.getThermalState.store(0);
    cache.telemetryProviderType = Telemetry::Provider::BROWSER;
    cache.telemetryProviderName = "Navigator";
    telemetryActive = true;
    scheduleTelemetryRefresh();

    // Print device information.

    JST_WARN("Due to current Emscripten limitations the device values are inaccurate.");
    JST_INFO("-----------------------------------------------------");
    JST_INFO("Jetstream Heterogeneous Backend [WebGPU]")
    JST_INFO("-----------------------------------------------------");
    JST_INFO("Device Name:     {}", getDeviceName());
    JST_INFO("Device Type:     {}", getPhysicalDeviceType());
    JST_INFO("API Version:     {}", getApiVersion());
    JST_INFO("Unified Memory:  {}", hasUnifiedMemory() ? "YES" : "NO");
    JST_INFO("Processor Count: {}", getTotalProcessorCount());
    JST_INFO("Device Memory:   {:.2f} GB", static_cast<F32>(getPhysicalMemory()) / (1024*1024*1024));
    JST_INFO("Staging Buffer:  {:.2f} MB", static_cast<F32>(config.stagingBufferSize) / JST_MB);
    JST_INFO("-----------------------------------------------------");
}

WebGPU::~WebGPU() {
    telemetryActive = false;
}

std::string WebGPU::getDeviceName() const {
    return cache.deviceName;
}

std::string WebGPU::getApiVersion() const {
    return cache.apiVersion;       
}

PhysicalDeviceType WebGPU::getPhysicalDeviceType() const {
    return cache.physicalDeviceType; 
}

bool WebGPU::hasUnifiedMemory() const {
    return cache.hasUnifiedMemory;
}

U64 WebGPU::getPhysicalMemory() const {
    return cache.physicalMemory;
}

U64 WebGPU::getTotalProcessorCount() const {
    return cache.totalProcessorCount;
}

bool WebGPU::getLowPowerStatus() const {
    return cache.lowPowerStatus.load();
}

U64 WebGPU::getThermalState() const {
    return cache.getThermalState.load();
}

void WebGPU::scheduleTelemetryRefresh() {
    if (!telemetryActive) {
        return;
    }
    emscripten_async_call(&WebGPU::TelemetryPump, this, 1000);
}

void WebGPU::TelemetryPump(void* userData) {
    auto* self = reinterpret_cast<WebGPU*>(userData);
    if (self == nullptr || !self->telemetryActive) {
        return;
    }
    self->refreshTelemetry();
    self->scheduleTelemetryRefresh();
}

void WebGPU::refreshTelemetry() {
    const int lowPowerHint = jetstream_webgpu_query_low_power_hint();
    if (lowPowerHint >= 0) {
        cache.lowPowerStatus.store(lowPowerHint == 1);
    } else if (!lowPowerWarningLogged) {
        JST_WARN("[WebGPU] Browser telemetry does not expose power hints.");
        lowPowerWarningLogged = true;
    }

    const int thermalBucket = jetstream_webgpu_query_thermal_bucket();
    if (thermalBucket >= 0) {
        cache.getThermalState.store(static_cast<U64>(thermalBucket));
    } else if (!thermalWarningLogged) {
        JST_WARN("[WebGPU] Browser telemetry does not expose thermal hints.");
        thermalWarningLogged = true;
    }
}

}  // namespace Jetstream::Backend
