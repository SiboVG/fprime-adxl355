#include "Adxl355/Platform/OsDelay.hpp"

#include "Fw/Time/TimeInterval.hpp"
#include "Os/Task.hpp"

namespace Adxl355 {

void OsDelay::delayMilliseconds(std::uint32_t milliseconds) {
    const U32 seconds = static_cast<U32>(milliseconds / 1000U);
    const U32 microseconds = static_cast<U32>((milliseconds % 1000U) * 1000U);
    (void)Os::Task::delay(Fw::TimeInterval(seconds, microseconds));
}

}  // namespace Adxl355
