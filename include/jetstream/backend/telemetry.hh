#ifndef JETSTREAM_BACKEND_TELEMETRY_HH
#define JETSTREAM_BACKEND_TELEMETRY_HH

#include "jetstream/types.hh"

namespace Jetstream::Backend::Telemetry {

enum class Provider : uint8_t {
    NONE = 0,
    NVML,
    RADEON_SMI,
    TOOLING_INFO,
    BROWSER,
};

constexpr U64 ThermalBucketFromCelsius(U32 temperatureC) {
    if (temperatureC >= 95) {
        return 3;
    }
    if (temperatureC >= 85) {
        return 2;
    }
    if (temperatureC >= 75) {
        return 1;
    }
    return 0;
}

constexpr bool IsLowPowerFromPState(U32 pState) {
    return pState >= 8;
}

constexpr bool IsLowPowerFromPowerBudget(U32 currentMilliwatts,
                                         U32 budgetMilliwatts,
                                         U32 utilizationThresholdPercent = 30) {
    if (budgetMilliwatts == 0) {
        return false;
    }
    const U32 utilizationPercent = static_cast<U32>((currentMilliwatts * 100ull) / budgetMilliwatts);
    return utilizationPercent < utilizationThresholdPercent;
}

}  // namespace Jetstream::Backend::Telemetry

#endif
