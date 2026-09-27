#ifndef ADXL355_PLATFORM_OS_DELAY_HPP
#define ADXL355_PLATFORM_OS_DELAY_HPP

#include "Adxl355/Device/Device.hpp"

namespace Adxl355 {

//! F Prime OS adapter for the portable Delay interface. Blocks the calling task.
class OsDelay final : public Delay {
  public:
    void delayMilliseconds(std::uint32_t milliseconds) override;
};

}  // namespace Adxl355

#endif
