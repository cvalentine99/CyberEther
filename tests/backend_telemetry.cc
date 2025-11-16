#include <cassert>

#include "jetstream/backend/telemetry.hh"
#include "jetstream/logger.hh"

using namespace Jetstream::Backend;

int main() {
    JST_LOG_SET_DEBUG_LEVEL(0);

    // Thermal buckets.
    assert(Telemetry::ThermalBucketFromCelsius(60) == 0);
    assert(Telemetry::ThermalBucketFromCelsius(76) == 1);
    assert(Telemetry::ThermalBucketFromCelsius(88) == 2);
    assert(Telemetry::ThermalBucketFromCelsius(100) == 3);

    // Low power helper heuristics.
    assert(Telemetry::IsLowPowerFromPState(2) == false);
    assert(Telemetry::IsLowPowerFromPState(9) == true);

    assert(Telemetry::IsLowPowerFromPowerBudget(10000, 60000));
    assert(Telemetry::IsLowPowerFromPowerBudget(40000, 60000) == false);

    return 0;
}
