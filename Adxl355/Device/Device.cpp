#include "Adxl355/Device/Device.hpp"

#include "Adxl355/Device/Assert.hpp"
#include "Adxl355/Device/Registers.hpp"

#include <algorithm>

namespace Adxl355 {

namespace {

static const double STANDARD_GRAVITY = 9.80665;
static const std::size_t IDENTITY_LENGTH = 4U;
static const std::size_t SAMPLE_DATA_LENGTH = 9U;
// STATUS (0x04) through ZDATA1 (0x10): status, FIFO entries, two temperature
// bytes, then the nine axis bytes.
static const std::size_t STATUS_BURST_LENGTH = 13U;
static const std::size_t STATUS_BURST_AXIS_OFFSET = 4U;
static const std::size_t FIFO_BYTES_PER_SAMPLE = 9U;
static const std::uint8_t FIFO_EMPTY_MARKER = 0x02U;
static const std::uint8_t FIFO_X_AXIS_MARKER = 0x01U;
static const std::size_t NVM_POLL_ATTEMPTS = 255U;
static const std::size_t TEMPERATURE_READ_ATTEMPTS = 3U;
// The storage temperature range (Rev. D, Table 5) is the widest the part is
// rated for; the uncalibrated output can err by a few degrees, so the
// operating range (-40 to +125 C) would reject real readings near its edges.
static const double MIN_PLAUSIBLE_CELSIUS = -55.0;
static const double MAX_PLAUSIBLE_CELSIUS = 150.0;
// No decoded 20-bit axis value equals this, so the first read after a start
// never counts as a repeat.
static const std::int32_t NO_PREVIOUS_SAMPLE = -0x7FFFFFFF - 1;
// Transfers of at most this many data bytes use a small stack frame, so the
// 4 kHz path does not clear FIFO-sized buffers on every read.
static const std::size_t SMALL_TRANSFER = 16U;

static_assert(Registers::SHADOW_LENGTH == 5U, "shadow reference storage is sized for five registers");
static_assert((Registers::FIFO_MAX_ENTRIES / 3U) * FIFO_BYTES_PER_SAMPLE <= Registers::MAX_REGISTER_TRANSFER,
              "a full FIFO must fit one register transfer");

bool validTransfer(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    return (data != nullptr) && (size != 0U) && (size <= Registers::MAX_REGISTER_TRANSFER) && (address <= 0x7FU);
}

template <std::size_t Capacity>
bool exchange(SpiBus& spi,
              std::uint8_t command,
              const std::uint8_t* writeData,
              std::uint8_t* readData,
              std::size_t size) {
    ADXL355_ASSERT((size + 1U) <= Capacity);
    std::uint8_t transmit[Capacity] = {0U};
    std::uint8_t receive[Capacity] = {0U};
    transmit[0] = command;
    if (writeData != nullptr) {
        for (std::size_t index = 0U; index < size; ++index) {
            transmit[index + 1U] = writeData[index];
        }
    }
    if (!spi.transfer(transmit, receive, size + 1U)) {
        return false;
    }
    if (readData != nullptr) {
        for (std::size_t index = 0U; index < size; ++index) {
            readData[index] = receive[index + 1U];
        }
    }
    return true;
}

bool exchangeSized(SpiBus& spi,
                   std::uint8_t command,
                   const std::uint8_t* writeData,
                   std::uint8_t* readData,
                   std::size_t size) {
    return (size <= SMALL_TRANSFER)
               ? exchange<SMALL_TRANSFER + 1U>(spi, command, writeData, readData, size)
               : exchange<Registers::MAX_SPI_TRANSACTION>(spi, command, writeData, readData, size);
}

}  // namespace

Configuration::Configuration()
    : range(Range::G2),
      outputDataRate(OdrSetting::HZ_4000),
      highPassCorner(HpfSetting::OFF),
      syncMode(SyncMode::INTERNAL),
      externalClock(false),
      temperatureEnabled(true),
      dataReadyEnabled(true),
      interruptActiveHigh(false),
      interruptMap(0U),
      fifoWatermark(96U) {}

Configuration Configuration::externalSynchronized4kHz() {
    Configuration configuration;
    configuration.outputDataRate = OdrSetting::HZ_4000;
    configuration.syncMode = SyncMode::EXTERNAL_NO_INTERPOLATION;
    configuration.externalClock = true;
    configuration.interruptMap = 0U;
    return configuration;
}

Identity::Identity() : analogDevicesId(0U), memsId(0U), partId(0U), revisionId(0U) {}

StatusRegister::StatusRegister()
    : dataReady(false), fifoFull(false), fifoOverrun(false), activity(false), nvmBusy(false), raw(0U) {}

RawSample::RawSample() : x(0), y(0), z(0) {}

Sample::Sample()
    : raw(), xMetersPerSecondSquared(0.0), yMetersPerSecondSquared(0.0), zMetersPerSecondSquared(0.0) {}

Temperature::Temperature() : raw(0U), celsius(0.0) {}

Device::Device(SpiBus& spi, Delay& delay)
    : m_spi(spi),
      m_delay(delay),
      m_configuration(),
      m_identity(),
      m_state(LifecycleState::UNCONFIGURED),
      m_selfTest(0U),
      m_lastStatus(0U),
      m_previousRaw(),
      m_repeats(0U),
      m_shadowCaptured(false),
      m_shadowReference() {
    this->restartRepeatCount();
}

Status Device::initialize(const Configuration& configuration) {
    Status status = Device::validateConfiguration(configuration);
    if (status != Status::OK) {
        return status;
    }

    Identity readBack;
    status = this->readIdentity(readBack);
    if (status != Status::OK) {
        return status;
    }
    status = this->softReset();
    if (status != Status::OK) {
        return status;
    }
    status = this->programConfiguration(configuration);
    if (status == Status::OK) {
        status = this->startMeasurement();
    }
    if (status != Status::OK) {
        // The reset has already been issued, so a partial initialization is
        // never left startable, whichever step failed.
        this->invalidate();
        return status;
    }
    this->m_identity = readBack;
    return Status::OK;
}

Status Device::readIdentity(Identity& deviceIdentity) {
    std::uint8_t values[IDENTITY_LENGTH] = {0U, 0U, 0U, 0U};
    const Status status = this->readRegister(Registers::DEVID_AD, values, IDENTITY_LENGTH);
    if (status != Status::OK) {
        return status;
    }
    deviceIdentity.analogDevicesId = values[0];
    deviceIdentity.memsId = values[1];
    deviceIdentity.partId = values[2];
    deviceIdentity.revisionId = values[3];
    if ((deviceIdentity.analogDevicesId != Registers::EXPECTED_DEVID_AD) ||
        (deviceIdentity.memsId != Registers::EXPECTED_DEVID_MST) ||
        (deviceIdentity.partId != Registers::EXPECTED_PARTID)) {
        return Status::IDENTITY_MISMATCH;
    }
    return Status::OK;
}

Status Device::softReset() {
    // Data sheet Rev. D, Reset register: a software reset can load NVM trim
    // incorrectly on REVID <= 0x01. Capture the shadow registers before the
    // first reset, compare after each reset, and repeat the reset on mismatch.
    Status status = this->captureShadowReference();
    if (status != Status::OK) {
        return status;
    }
    for (std::uint8_t attempt = 0U; attempt < MAX_RESET_ATTEMPTS; ++attempt) {
        status = this->resetOnce();
        if (status != Status::OK) {
            return status;
        }
        bool matches = false;
        status = this->compareShadow(matches);
        if (status != Status::OK) {
            return status;
        }
        if (matches) {
            return Status::OK;
        }
    }
    return Status::NVM_MISMATCH;
}

Status Device::resetOnce() {
    const std::uint8_t resetCode = Registers::RESET_CODE;
    const Status status = this->transferWrite(Registers::RESET, &resetCode, 1U);
    // Invalidate even when the transfer reports failure: the reset may have
    // reached the device.
    this->invalidate();
    this->m_configuration = Configuration();
    this->m_selfTest = 0U;
    if (status != Status::OK) {
        return status;
    }

    this->m_delay.delayMilliseconds(1U);
    for (std::size_t attempt = 0U; attempt < NVM_POLL_ATTEMPTS; ++attempt) {
        StatusRegister deviceStatus;
        const Status pollStatus = this->readStatus(deviceStatus);
        if (pollStatus != Status::OK) {
            return pollStatus;
        }
        if (!deviceStatus.nvmBusy) {
            return Status::OK;
        }
        this->m_delay.delayMilliseconds(1U);
    }
    return Status::TIMEOUT;
}

Status Device::captureShadowReference() {
    if (this->m_shadowCaptured) {
        return Status::OK;
    }
    const Status status =
        this->readRegister(Registers::SHADOW_START, this->m_shadowReference, Registers::SHADOW_LENGTH);
    if (status == Status::OK) {
        this->m_shadowCaptured = true;
    }
    return status;
}

Status Device::compareShadow(bool& matches) {
    matches = false;
    std::uint8_t current[Registers::SHADOW_LENGTH] = {0U};
    const Status status = this->readRegister(Registers::SHADOW_START, current, Registers::SHADOW_LENGTH);
    if (status != Status::OK) {
        return status;
    }
    matches = std::equal(current, current + Registers::SHADOW_LENGTH, this->m_shadowReference);
    return Status::OK;
}

Status Device::programConfiguration(const Configuration& configuration) {
    // Called only after a verified reset, so the device is in standby and the
    // lifecycle is UNCONFIGURED until every register has been read back.
    this->m_configuration = configuration;
    const std::uint8_t standbyValue = this->powerControlValue(true);
    Status status = this->transferWrite(Registers::POWER_CTL, &standbyValue, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::POWER_CTL, standbyValue, 0x07U);
    }
    if (status != Status::OK) {
        return status;
    }

    const std::uint8_t filterValue = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(configuration.highPassCorner) << 4U) |
        static_cast<std::uint8_t>(configuration.outputDataRate));
    status = this->transferWrite(Registers::FILTER, &filterValue, 1U);
    if (status != Status::OK) {
        return status;
    }

    std::uint8_t currentRange = 0U;
    status = this->readRegister(Registers::RANGE, &currentRange, 1U);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t rangeValue = static_cast<std::uint8_t>(
        (currentRange & Registers::RANGE_I2C_HIGH_SPEED) |
        (configuration.interruptActiveHigh ? Registers::RANGE_INTERRUPT_POLARITY : 0U) |
        static_cast<std::uint8_t>(configuration.range));
    status = this->transferWrite(Registers::RANGE, &rangeValue, 1U);
    if (status != Status::OK) {
        return status;
    }

    status = this->transferWrite(Registers::FIFO_SAMPLES, &configuration.fifoWatermark, 1U);
    if (status != Status::OK) {
        return status;
    }
    status = this->transferWrite(Registers::INT_MAP, &configuration.interruptMap, 1U);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t syncValue = static_cast<std::uint8_t>(
        (configuration.externalClock ? Registers::SYNC_EXTERNAL_CLOCK : 0U) |
        static_cast<std::uint8_t>(configuration.syncMode));
    status = this->transferWrite(Registers::SYNC, &syncValue, 1U);
    if (status != Status::OK) {
        return status;
    }

    status = this->verifyRegister(Registers::FILTER, filterValue, 0x7FU);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::FIFO_SAMPLES, configuration.fifoWatermark, 0x7FU);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::INT_MAP, configuration.interruptMap, 0xFFU);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::SYNC, syncValue, 0x07U);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::RANGE, rangeValue, 0xC3U);
    if (status != Status::OK) {
        return status;
    }

    this->m_state = LifecycleState::STANDBY;
    return Status::OK;
}

Status Device::startMeasurement() {
    if (this->m_state == LifecycleState::UNCONFIGURED) {
        return Status::INVALID_STATE;
    }
    const std::uint8_t powerValue = this->powerControlValue(false);
    Status status = this->transferWrite(Registers::POWER_CTL, &powerValue, 1U);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::POWER_CTL, powerValue, 0x07U);
    if (status != Status::OK) {
        // Hardware state is uncertain: require full reinitialization.
        this->invalidate();
        return status;
    }
    this->m_state = LifecycleState::MEASURING;
    this->restartRepeatCount();
    return Status::OK;
}

Status Device::stopMeasurement() {
    // Standby is the safe state, so it is allowed from every lifecycle state.
    const std::uint8_t powerValue = this->powerControlValue(true);
    Status status = this->transferWrite(Registers::POWER_CTL, &powerValue, 1U);
    if (status != Status::OK) {
        return status;
    }
    status = this->verifyRegister(Registers::POWER_CTL, powerValue, 0x07U);
    if (status != Status::OK) {
        this->invalidate();
        return status;
    }
    if (this->m_state == LifecycleState::MEASURING) {
        this->m_state = LifecycleState::STANDBY;
    }
    return Status::OK;
}

Status Device::setRange(Range rangeValue) {
    const std::uint8_t encodedRange = static_cast<std::uint8_t>(rangeValue);
    if ((encodedRange < 1U) || (encodedRange > 3U)) {
        return Status::INVALID_ARGUMENT;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }

    std::uint8_t current = 0U;
    status = this->readRegister(Registers::RANGE, &current, 1U);
    if (status == Status::OK) {
        const std::uint8_t updated = static_cast<std::uint8_t>(
            (current & static_cast<std::uint8_t>(Registers::RANGE_I2C_HIGH_SPEED |
                                                Registers::RANGE_INTERRUPT_POLARITY)) |
            encodedRange);
        status = this->transferWrite(Registers::RANGE, &updated, 1U);
        if (status == Status::OK) {
            status = this->verifyRegister(Registers::RANGE, updated, 0xC3U);
        }
    }
    if (status == Status::OK) {
        this->m_configuration.range = rangeValue;
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::setFilter(OdrSetting outputDataRate, HpfSetting highPassCorner) {
    if ((static_cast<std::uint8_t>(outputDataRate) > 10U) ||
        (static_cast<std::uint8_t>(highPassCorner) > 6U)) {
        return Status::INVALID_ARGUMENT;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t value = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(highPassCorner) << 4U) | static_cast<std::uint8_t>(outputDataRate));
    status = this->transferWrite(Registers::FILTER, &value, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::FILTER, value, 0x7FU);
    }
    if (status == Status::OK) {
        this->m_configuration.outputDataRate = outputDataRate;
        this->m_configuration.highPassCorner = highPassCorner;
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::setSynchronization(SyncMode syncMode, bool externalClock) {
    if (static_cast<std::uint8_t>(syncMode) > 2U) {
        return Status::INVALID_ARGUMENT;
    }
    // Data sheet Table 14: EXT_SYNC = 01 without EXT_CLK resets the digital
    // filters on every synchronization pulse and is not recommended.
    if ((syncMode == SyncMode::EXTERNAL_NO_INTERPOLATION) && !externalClock) {
        return Status::INVALID_CONFIGURATION;
    }
    if (externalClock && ((this->m_configuration.interruptMap & Registers::INT_MAP_INT2) != 0U)) {
        return Status::INVALID_CONFIGURATION;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t value = static_cast<std::uint8_t>(
        (externalClock ? Registers::SYNC_EXTERNAL_CLOCK : 0U) | static_cast<std::uint8_t>(syncMode));
    status = this->transferWrite(Registers::SYNC, &value, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::SYNC, value, 0x07U);
    }
    if (status == Status::OK) {
        this->m_configuration.syncMode = syncMode;
        this->m_configuration.externalClock = externalClock;
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::setOffsets(std::int16_t x, std::int16_t y, std::int16_t z) {
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    const std::uint16_t xBits = static_cast<std::uint16_t>(x);
    const std::uint16_t yBits = static_cast<std::uint16_t>(y);
    const std::uint16_t zBits = static_cast<std::uint16_t>(z);
    const std::uint8_t values[6] = {
        static_cast<std::uint8_t>(xBits >> 8U), static_cast<std::uint8_t>(xBits & 0xFFU),
        static_cast<std::uint8_t>(yBits >> 8U), static_cast<std::uint8_t>(yBits & 0xFFU),
        static_cast<std::uint8_t>(zBits >> 8U), static_cast<std::uint8_t>(zBits & 0xFFU),
    };
    status = this->transferWrite(Registers::OFFSET_X_H, values, 6U);
    if (status == Status::OK) {
        std::uint8_t actual[6] = {0U};
        status = this->readRegister(Registers::OFFSET_X_H, actual, 6U);
        if ((status == Status::OK) && !std::equal(actual, actual + 6U, values)) {
            status = Status::VERIFY_ERROR;
        }
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::configureActivity(std::uint8_t axisMask, std::uint16_t threshold, std::uint8_t count) {
    if (((axisMask & 0xF8U) != 0U) || (count == 0U)) {
        return Status::INVALID_ARGUMENT;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t values[4] = {
        axisMask,
        static_cast<std::uint8_t>(threshold >> 8U),
        static_cast<std::uint8_t>(threshold & 0xFFU),
        count,
    };
    status = this->transferWrite(Registers::ACT_EN, values, 4U);
    if (status == Status::OK) {
        std::uint8_t actual[4] = {0U};
        status = this->readRegister(Registers::ACT_EN, actual, 4U);
        if ((status == Status::OK) && !std::equal(actual, actual + 4U, values)) {
            status = Status::VERIFY_ERROR;
        }
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::configureInterrupts(std::uint8_t interruptMap, bool activeHigh) {
    if (this->m_configuration.externalClock && ((interruptMap & Registers::INT_MAP_INT2) != 0U)) {
        return Status::INVALID_CONFIGURATION;
    }
    // INT_MAP may change while measuring, but INT_POL lives in the range
    // register, which the data sheet only allows to change in standby.
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    status = this->transferWrite(Registers::INT_MAP, &interruptMap, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::INT_MAP, interruptMap, 0xFFU);
    }
    std::uint8_t rangeValue = 0U;
    if (status == Status::OK) {
        status = this->readRegister(Registers::RANGE, &rangeValue, 1U);
    }
    if (status == Status::OK) {
        rangeValue = static_cast<std::uint8_t>(
            (rangeValue & static_cast<std::uint8_t>(~Registers::RANGE_INTERRUPT_POLARITY)) |
            (activeHigh ? Registers::RANGE_INTERRUPT_POLARITY : 0U));
        status = this->transferWrite(Registers::RANGE, &rangeValue, 1U);
    }
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::RANGE, rangeValue, 0xC3U);
    }
    if (status == Status::OK) {
        this->m_configuration.interruptMap = interruptMap;
        this->m_configuration.interruptActiveHigh = activeHigh;
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::setSelfTest(bool enableMode, bool applyForce) {
    if (applyForce && !enableMode) {
        return Status::INVALID_ARGUMENT;
    }
    if (this->m_state == LifecycleState::UNCONFIGURED) {
        return Status::INVALID_STATE;
    }
    // ST1/ST2 may change in measurement mode (data sheet register map note).
    const std::uint8_t value = static_cast<std::uint8_t>((enableMode ? Registers::SELF_TEST_MODE : 0U) |
                                                          (applyForce ? Registers::SELF_TEST_FORCE : 0U));
    Status status = this->transferWrite(Registers::SELF_TEST, &value, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::SELF_TEST, value, 0x03U);
    }
    if (status != Status::OK) {
        // The stimulus may be applied: stop trusting samples until a reset
        // clears the self-test bits.
        this->invalidate();
        return status;
    }
    this->m_selfTest = value;
    return Status::OK;
}

Status Device::setFifoWatermark(std::uint8_t watermark) {
    if ((watermark < 1U) || (watermark > 96U)) {
        return Status::INVALID_ARGUMENT;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    status = this->transferWrite(Registers::FIFO_SAMPLES, &watermark, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::FIFO_SAMPLES, watermark, 0x7FU);
    }
    if (status == Status::OK) {
        this->m_configuration.fifoWatermark = watermark;
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::readStatus(StatusRegister& statusRegister) {
    std::uint8_t value = 0U;
    const Status status = this->readRegister(Registers::STATUS, &value, 1U);
    if (status != Status::OK) {
        return status;
    }
    this->m_lastStatus = value;
    statusRegister.raw = value;
    statusRegister.dataReady = (value & Registers::STATUS_DATA_READY) != 0U;
    statusRegister.fifoFull = (value & Registers::STATUS_FIFO_FULL) != 0U;
    statusRegister.fifoOverrun = (value & Registers::STATUS_FIFO_OVERRUN) != 0U;
    statusRegister.activity = (value & Registers::STATUS_ACTIVITY) != 0U;
    statusRegister.nvmBusy = (value & Registers::STATUS_NVM_BUSY) != 0U;
    return Status::OK;
}

Status Device::readSample(Sample& sample) {
    std::uint8_t bytes[STATUS_BURST_LENGTH] = {0U};
    const Status status = this->readRegister(Registers::STATUS, bytes, STATUS_BURST_LENGTH);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t statusValue = bytes[0];
    this->m_lastStatus = statusValue;
    // STATUS bits 7:5 always read 0. Nonzero reserved bits mean the bytes did
    // not come from an ADXL355, for example a floating or stuck-high MISO.
    if ((statusValue & Registers::STATUS_RESERVED) != 0U) {
        return Status::IMPLAUSIBLE_DATA;
    }
    // DATA_RDY clears on every STATUS read and sets again only for newer data,
    // so a clear bit means these axis bytes were already delivered or the
    // device is not converting (for example, an external clock that never ran).
    if ((statusValue & Registers::STATUS_DATA_READY) == 0U) {
        return Status::STALE_DATA;
    }
    RawSample raw;
    raw.x = Device::decodeAcceleration(&bytes[STATUS_BURST_AXIS_OFFSET]);
    raw.y = Device::decodeAcceleration(&bytes[STATUS_BURST_AXIS_OFFSET + 3U]);
    raw.z = Device::decodeAcceleration(&bytes[STATUS_BURST_AXIS_OFFSET + 6U]);
    // The digital core runs on the external clock. A clock that stops after
    // running leaves DATA_RDY set, STATUS reads no longer clear it, and the axis
    // registers keep the last sample (TE0715 bench, 2026-10-02), so a set bit
    // alone does not prove a new sample.
    if (this->repeatsPreviousSample(raw)) {
        return Status::FROZEN_DATA;
    }
    this->scale(raw, sample);
    return Status::OK;
}

Status Device::readRawSample(RawSample& sample) {
    std::uint8_t bytes[SAMPLE_DATA_LENGTH] = {0U};
    const Status status = this->readRegister(Registers::XDATA3, bytes, SAMPLE_DATA_LENGTH);
    if (status != Status::OK) {
        return status;
    }
    sample.x = Device::decodeAcceleration(&bytes[0]);
    sample.y = Device::decodeAcceleration(&bytes[3]);
    sample.z = Device::decodeAcceleration(&bytes[6]);
    return Status::OK;
}

void Device::scale(const RawSample& raw, Sample& sample) const {
    const double lsbPerG = Device::sensitivityLsbPerG(this->m_configuration.range);
    sample.raw = raw;
    sample.xMetersPerSecondSquared = (static_cast<double>(raw.x) / lsbPerG) * STANDARD_GRAVITY;
    sample.yMetersPerSecondSquared = (static_cast<double>(raw.y) / lsbPerG) * STANDARD_GRAVITY;
    sample.zMetersPerSecondSquared = (static_cast<double>(raw.z) / lsbPerG) * STANDARD_GRAVITY;
}

Status Device::readTemperature(Temperature& temperature) {
    // TEMP2/TEMP1 are not double buffered and can update between the two
    // bytes, so accept a value only when two consecutive reads agree.
    std::uint16_t previous = 0U;
    for (std::size_t attempt = 0U; attempt < TEMPERATURE_READ_ATTEMPTS; ++attempt) {
        std::uint8_t bytes[2] = {0U, 0U};
        const Status status = this->readRegister(Registers::TEMP2, bytes, 2U);
        if (status != Status::OK) {
            return status;
        }
        // Reserved bits read 0; set bits mean the bytes did not come from an
        // ADXL355, for example a stuck-high MISO.
        if ((bytes[0] & Registers::TEMP2_RESERVED) != 0U) {
            return Status::IMPLAUSIBLE_DATA;
        }
        const std::uint16_t current = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[0]) << 8U) | static_cast<std::uint16_t>(bytes[1]));
        if ((attempt > 0U) && (current == previous)) {
            const double celsius = ((static_cast<double>(current) - 1885.0) / -9.05) + 25.0;
            // A stuck-low MISO reads raw 0, about +233 C.
            if ((celsius < MIN_PLAUSIBLE_CELSIUS) || (celsius > MAX_PLAUSIBLE_CELSIUS)) {
                return Status::IMPLAUSIBLE_DATA;
            }
            temperature.raw = current;
            temperature.celsius = celsius;
            return Status::OK;
        }
        previous = current;
    }
    return Status::IMPLAUSIBLE_DATA;
}

Status Device::readFifo(RawSample* samples, std::size_t capacity, std::size_t& count) {
    count = 0U;
    if ((samples == nullptr) || (capacity == 0U)) {
        return Status::INVALID_ARGUMENT;
    }

    std::uint8_t entries = 0U;
    Status status = this->readRegister(Registers::FIFO_ENTRIES, &entries, 1U);
    if (status != Status::OK) {
        return status;
    }
    const std::size_t entryCount = static_cast<std::size_t>(entries & 0x7FU);
    // The FIFO holds 0 through 96 entries; anything larger is not device data.
    if (entryCount > Registers::FIFO_MAX_ENTRIES) {
        return Status::IMPLAUSIBLE_DATA;
    }
    if ((entryCount % 3U) != 0U) {
        return Status::FIFO_MISALIGNED;
    }
    const std::size_t availableSamples = entryCount / 3U;
    const std::size_t samplesToRead = std::min(availableSamples, capacity);
    if (samplesToRead == 0U) {
        return Status::OK;
    }
    ADXL355_ASSERT((samplesToRead * FIFO_BYTES_PER_SAMPLE) <= Registers::MAX_REGISTER_TRANSFER);

    std::uint8_t bytes[Registers::MAX_REGISTER_TRANSFER] = {0U};
    status = this->readRegister(Registers::FIFO_DATA, bytes, samplesToRead * FIFO_BYTES_PER_SAMPLE);
    if (status != Status::OK) {
        return status;
    }

    for (std::size_t sampleIndex = 0U; sampleIndex < samplesToRead; ++sampleIndex) {
        const std::size_t offset = sampleIndex * FIFO_BYTES_PER_SAMPLE;
        const std::uint8_t xFlags = bytes[offset + 2U];
        const std::uint8_t yFlags = bytes[offset + 5U];
        const std::uint8_t zFlags = bytes[offset + 8U];
        if (((xFlags | yFlags | zFlags) & FIFO_EMPTY_MARKER) != 0U) {
            return Status::FIFO_EMPTY;
        }
        if (((xFlags & FIFO_X_AXIS_MARKER) == 0U) || ((yFlags & FIFO_X_AXIS_MARKER) != 0U) ||
            ((zFlags & FIFO_X_AXIS_MARKER) != 0U)) {
            return Status::FIFO_MISALIGNED;
        }
        samples[sampleIndex].x = Device::decodeAcceleration(&bytes[offset]);
        samples[sampleIndex].y = Device::decodeAcceleration(&bytes[offset + 3U]);
        samples[sampleIndex].z = Device::decodeAcceleration(&bytes[offset + 6U]);
    }
    count = samplesToRead;
    return Status::OK;
}

Status Device::readRegister(std::uint8_t address, std::uint8_t* data, std::size_t size) {
    if (!validTransfer(address, data, size)) {
        return Status::INVALID_ARGUMENT;
    }
    return exchangeSized(this->m_spi, Registers::readCommand(address), nullptr, data, size) ? Status::OK
                                                                                            : Status::SPI_ERROR;
}

Status Device::writeRegister(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    if (!validTransfer(address, data, size)) {
        return Status::INVALID_ARGUMENT;
    }
    const Status status = this->transferWrite(address, data, size);
    this->invalidate();
    return status;
}

Status Device::transferWrite(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    if (!validTransfer(address, data, size)) {
        return Status::INVALID_ARGUMENT;
    }
    return exchangeSized(this->m_spi, Registers::writeCommand(address), data, nullptr, size) ? Status::OK
                                                                                             : Status::SPI_ERROR;
}

LifecycleState Device::state() const {
    return this->m_state;
}

bool Device::isInitialized() const {
    return this->m_state != LifecycleState::UNCONFIGURED;
}

bool Device::isMeasuring() const {
    return this->m_state == LifecycleState::MEASURING;
}

Range Device::range() const {
    return this->m_configuration.range;
}

const Identity& Device::identity() const {
    return this->m_identity;
}

std::uint8_t Device::selfTestBits() const {
    return this->m_selfTest;
}

std::uint8_t Device::lastStatusRegister() const {
    return this->m_lastStatus;
}

Status Device::validateConfiguration(const Configuration& configuration) {
    const std::uint8_t rangeValue = static_cast<std::uint8_t>(configuration.range);
    const std::uint8_t odrValue = static_cast<std::uint8_t>(configuration.outputDataRate);
    const std::uint8_t hpfValue = static_cast<std::uint8_t>(configuration.highPassCorner);
    const std::uint8_t syncValue = static_cast<std::uint8_t>(configuration.syncMode);
    if ((rangeValue < 1U) || (rangeValue > 3U) || (odrValue > 10U) || (hpfValue > 6U) ||
        (syncValue > 2U) || (configuration.fifoWatermark < 1U) || (configuration.fifoWatermark > 96U)) {
        return Status::INVALID_CONFIGURATION;
    }
    if (configuration.externalClock && ((configuration.interruptMap & Registers::INT_MAP_INT2) != 0U)) {
        return Status::INVALID_CONFIGURATION;
    }
    if ((configuration.syncMode == SyncMode::EXTERNAL_NO_INTERPOLATION) && !configuration.externalClock) {
        return Status::INVALID_CONFIGURATION;
    }
    return Status::OK;
}

Status Device::verifyRegister(std::uint8_t address, std::uint8_t expected, std::uint8_t mask) {
    std::uint8_t actual = 0U;
    const Status status = this->readRegister(address, &actual, 1U);
    if (status != Status::OK) {
        return status;
    }
    return ((actual & mask) == (expected & mask)) ? Status::OK : Status::VERIFY_ERROR;
}

Status Device::enterStandbyForConfiguration(bool& resumeMeasurement) {
    resumeMeasurement = false;
    if (this->m_state == LifecycleState::UNCONFIGURED) {
        return Status::INVALID_STATE;
    }
    resumeMeasurement = this->m_state == LifecycleState::MEASURING;
    return resumeMeasurement ? this->stopMeasurement() : Status::OK;
}

Status Device::finishConfigurationChange(bool resumeMeasurement, Status operationStatus) {
    if (operationStatus != Status::OK) {
        // A failed runtime update can leave hardware and software with
        // different register values. Require full initialization before a
        // later START command may re-enter measurement mode.
        this->invalidate();
        return operationStatus;
    }
    return resumeMeasurement ? this->startMeasurement() : Status::OK;
}

void Device::invalidate() {
    this->m_state = LifecycleState::UNCONFIGURED;
}

bool Device::repeatsPreviousSample(const RawSample& raw) {
    // Noise makes an exact repeat of all three 20-bit axes rare (about 1e-7 per
    // read at 4 kHz and +/-2 g), so FROZEN_REPEAT_LIMIT repeats in a row stay
    // improbable even at 3.906 Hz and +/-8 g, where the noise is about 1.5 LSB.
    const bool repeat = (raw.x == this->m_previousRaw.x) && (raw.y == this->m_previousRaw.y) &&
                        (raw.z == this->m_previousRaw.z);
    this->m_previousRaw = raw;
    if (!repeat) {
        this->m_repeats = 0U;
        return false;
    }
    if (this->m_repeats < FROZEN_REPEAT_LIMIT) {
        ++this->m_repeats;
    }
    return this->m_repeats >= FROZEN_REPEAT_LIMIT;
}

void Device::restartRepeatCount() {
    this->m_previousRaw.x = NO_PREVIOUS_SAMPLE;
    this->m_previousRaw.y = NO_PREVIOUS_SAMPLE;
    this->m_previousRaw.z = NO_PREVIOUS_SAMPLE;
    this->m_repeats = 0U;
}

std::uint8_t Device::powerControlValue(bool standby) const {
    return static_cast<std::uint8_t>((standby ? Registers::POWER_STANDBY : 0U) |
                                     (this->m_configuration.temperatureEnabled ? 0U : Registers::POWER_TEMP_OFF) |
                                     (this->m_configuration.dataReadyEnabled ? 0U : Registers::POWER_DRDY_OFF));
}

std::int32_t Device::decodeAcceleration(const std::uint8_t* bytes) {
    std::uint32_t value = ((static_cast<std::uint32_t>(bytes[0]) << 16U) |
                           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                           static_cast<std::uint32_t>(bytes[2])) >>
                          4U;
    if ((value & 0x00080000U) != 0U) {
        value |= 0xFFF00000U;
    }
    return static_cast<std::int32_t>(value);
}

double Device::sensitivityLsbPerG(Range rangeValue) {
    // Typical sensitivities, data sheet Rev. D Table 2.
    switch (rangeValue) {
        case Range::G2:
            return 256000.0;
        case Range::G4:
            return 128000.0;
        case Range::G8:
            return 64000.0;
    }
    // Unreachable: a range is validated before it is stored.
    ADXL355_ASSERT(false);
    return 256000.0;
}

}  // namespace Adxl355
