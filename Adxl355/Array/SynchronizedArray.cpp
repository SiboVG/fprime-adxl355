#include "Adxl355/Array/SynchronizedArray.hpp"

namespace Adxl355 {

SynchronizedFrame::SynchronizedFrame()
    : sequence(0U), syncTimeMicroseconds(0U), validMask(0U), sensorCount(0U), samples(), sensorStatus() {
    for (std::size_t index = 0U; index < MAX_SENSORS; ++index) {
        sensorStatus[index] = Status::INVALID_STATE;
    }
}

SynchronizedArray::SynchronizedArray() : m_devices(), m_count(0U), m_sequence(0U) {
    for (std::size_t index = 0U; index < MAX_SENSORS; ++index) {
        m_devices[index] = nullptr;
    }
}

Status SynchronizedArray::add(Device& device) {
    if (this->m_count >= MAX_SENSORS) {
        return Status::CAPACITY_EXCEEDED;
    }
    this->m_devices[this->m_count] = &device;
    ++this->m_count;
    return Status::OK;
}

Status SynchronizedArray::initializeAll(const Configuration& configuration, std::uint8_t& initializedMask) {
    initializedMask = 0U;
    if (this->m_count == 0U) {
        return Status::INVALID_STATE;
    }
    bool allInitialized = true;
    for (std::size_t index = 0U; index < this->m_count; ++index) {
        const Status status = this->m_devices[index]->initialize(configuration);
        if (status == Status::OK) {
            initializedMask = static_cast<std::uint8_t>(initializedMask | (1U << index));
        } else {
            allInitialized = false;
        }
    }
    return allInitialized ? Status::OK : Status::PARTIAL;
}

Status SynchronizedArray::readFrame(std::uint64_t syncTimeMicroseconds, SynchronizedFrame& frame) {
    frame = SynchronizedFrame();
    frame.sequence = this->m_sequence;
    frame.syncTimeMicroseconds = syncTimeMicroseconds;
    frame.sensorCount = this->m_count;
    ++this->m_sequence;

    if (this->m_count == 0U) {
        return Status::INVALID_STATE;
    }

    bool allRead = true;
    for (std::size_t index = 0U; index < this->m_count; ++index) {
        Device& device = *this->m_devices[index];
        frame.sensorStatus[index] = device.isMeasuring()
                                        ? device.readSample(frame.samples[index])
                                        : Status::INVALID_STATE;
        if (frame.sensorStatus[index] == Status::OK) {
            frame.validMask = static_cast<std::uint8_t>(frame.validMask | (1U << index));
        } else {
            allRead = false;
        }
    }
    return allRead ? Status::OK : Status::PARTIAL;
}

std::size_t SynchronizedArray::size() const {
    return this->m_count;
}

}  // namespace Adxl355
