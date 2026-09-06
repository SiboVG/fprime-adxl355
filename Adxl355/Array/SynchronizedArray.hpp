#ifndef ADXL355_ARRAY_SYNCHRONIZED_ARRAY_HPP
#define ADXL355_ARRAY_SYNCHRONIZED_ARRAY_HPP

#include "Adxl355/Device/Device.hpp"

#include <cstddef>
#include <cstdint>

namespace Adxl355 {

struct SynchronizedFrame {
    static const std::size_t MAX_SENSORS = 7U;

    SynchronizedFrame();

    std::uint64_t sequence;
    std::uint64_t syncTimeMicroseconds;
    std::uint8_t validMask;
    std::size_t sensorCount;
    Sample samples[MAX_SENSORS];
    Status sensorStatus[MAX_SENSORS];
};

class SynchronizedArray {
  public:
    static const std::size_t MAX_SENSORS = SynchronizedFrame::MAX_SENSORS;

    SynchronizedArray();

    Status add(Device& device);
    Status initializeAll(const Configuration& configuration, std::uint8_t& initializedMask);
    Status readFrame(std::uint64_t syncTimeMicroseconds, SynchronizedFrame& frame);
    std::size_t size() const;

  private:
    Device* m_devices[MAX_SENSORS];
    std::size_t m_count;
    std::uint64_t m_sequence;
};

}  // namespace Adxl355

#endif
