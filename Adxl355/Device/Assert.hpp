#ifndef ADXL355_DEVICE_ASSERT_HPP
#define ADXL355_DEVICE_ASSERT_HPP

// Assertions for conditions that can only fail through a software defect,
// never for bus, ground, or configuration input. The F Prime module build
// defines ADXL355_USE_FW_ASSERT so the deployment's FW_ASSERT hook defines the
// response. Other platforms may define ADXL355_ASSERT before including this
// header; otherwise a failed assertion aborts.
#if defined(ADXL355_USE_FW_ASSERT)
#include "Fw/Types/Assert.hpp"
#define ADXL355_ASSERT(condition) FW_ASSERT(condition)
#elif !defined(ADXL355_ASSERT)
#include <cstdlib>
#define ADXL355_ASSERT(condition) \
    do {                          \
        if (!(condition)) {       \
            std::abort();         \
        }                         \
    } while (false)
#endif

#endif
