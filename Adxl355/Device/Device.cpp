#include "Adxl355/Device/Device.hpp"

#include "Adxl355/Device/Registers.hpp"

#include <algorithm>

namespace Adxl355 {

namespace {

static const double STANDARD_GRAVITY = 9.80665;
static const std::size_t IDENTITY_LENGTH = 4U;
static const std::size_t SAMPLE_DATA_LENGTH = 9U;
static const std::size_t FIFO_BYTES_PER_SAMPLE = 9U;
static const std::uint8_t FIFO_EMPTY_MARKER = 0x02U;
static const std::uint8_t FIFO_X_AXIS_MARKER = 0x01U;

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
      m_configured(false),
      m_initialized(false),
      m_measuring(false) {}

Status Device::initialize(const Configuration& configuration) {
    Status status = this->validateConfiguration(configuration);
    if (status != Status::OK) {
        return status;
    }

    Identity identity;
    status = this->readIdentity(identity);
    if (status != Status::OK) {
        return status;
    }
    status = this->softReset();
    if (status != Status::OK) {
        return status;
    }
    status = this->configure(configuration);
    if (status == Status::OK) {
        this->m_initialized = true;
    }
    return status;
}

Status Device::readIdentity(Identity& identity) {
    std::uint8_t values[IDENTITY_LENGTH] = {0U, 0U, 0U, 0U};
    const Status status = this->readRegister(Registers::DEVID_AD, values, IDENTITY_LENGTH);
    if (status != Status::OK) {
        return status;
    }
    identity.analogDevicesId = values[0];
    identity.memsId = values[1];
    identity.partId = values[2];
    identity.revisionId = values[3];
    if ((identity.analogDevicesId != Registers::EXPECTED_DEVID_AD) ||
        (identity.memsId != Registers::EXPECTED_DEVID_MST) ||
        (identity.partId != Registers::EXPECTED_PARTID)) {
        return Status::IDENTITY_MISMATCH;
    }
    return Status::OK;
}

Status Device::softReset() {
    const std::uint8_t resetCode = Registers::RESET_CODE;
    Status status = this->writeRegister(Registers::RESET, &resetCode, 1U);
    if (status != Status::OK) {
        return status;
    }

    // The reset command immediately invalidates the software's previous view
    // of the device, even if NVM loading later times out.
    this->m_configured = false;
    this->m_initialized = false;
    this->m_measuring = false;
    this->m_configuration = Configuration();
    this->m_delay.delayMilliseconds(1U);
    for (std::size_t attempt = 0U; attempt < 255U; ++attempt) {
        StatusRegister deviceStatus;
        status = this->readStatus(deviceStatus);
        if (status != Status::OK) {
            return status;
        }
        if (!deviceStatus.nvmBusy) {
            return Status::OK;
        }
        this->m_delay.delayMilliseconds(1U);
    }
    return Status::TIMEOUT;
}

Status Device::configure(const Configuration& configuration) {
    Status status = this->validateConfiguration(configuration);
    if (status != Status::OK) {
        return status;
    }
    if (this->m_measuring) {
        status = this->stopMeasurement();
        if (status != Status::OK) {
            return status;
        }
    }

    // From this point onward, any failed write or readback leaves a potentially
    // mixed register set. Do not allow measurement to restart until a complete
    // configuration has been programmed and verified.
    this->m_configured = false;
    this->m_initialized = false;
    this->m_configuration = configuration;
    const std::uint8_t standbyValue = this->powerControlValue(true);
    status = this->writeRegister(Registers::POWER_CTL, &standbyValue, 1U);
    if (status != Status::OK) {
        return status;
    }

    const std::uint8_t filterValue = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(configuration.highPassCorner) << 4U) |
        static_cast<std::uint8_t>(configuration.outputDataRate));
    status = this->writeRegister(Registers::FILTER, &filterValue, 1U);
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
    status = this->writeRegister(Registers::RANGE, &rangeValue, 1U);
    if (status != Status::OK) {
        return status;
    }

    status = this->writeRegister(Registers::FIFO_SAMPLES, &configuration.fifoWatermark, 1U);
    if (status != Status::OK) {
        return status;
    }
    status = this->writeRegister(Registers::INT_MAP, &configuration.interruptMap, 1U);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t syncValue = static_cast<std::uint8_t>(
        (configuration.externalClock ? Registers::SYNC_EXTERNAL_CLOCK : 0U) |
        static_cast<std::uint8_t>(configuration.syncMode));
    status = this->writeRegister(Registers::SYNC, &syncValue, 1U);
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

    this->m_configured = true;
    return this->startMeasurement();
}

Status Device::startMeasurement() {
    if (!this->m_configured) {
        return Status::INVALID_STATE;
    }
    const std::uint8_t powerValue = this->powerControlValue(false);
    const Status status = this->writeRegister(Registers::POWER_CTL, &powerValue, 1U);
    if (status == Status::OK) {
        this->m_measuring = true;
    }
    return status;
}

Status Device::stopMeasurement() {
    const std::uint8_t powerValue = this->powerControlValue(true);
    const Status status = this->writeRegister(Registers::POWER_CTL, &powerValue, 1U);
    if (status == Status::OK) {
        this->m_measuring = false;
    }
    return status;
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
        status = this->writeRegister(Registers::RANGE, &updated, 1U);
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
    status = this->writeRegister(Registers::FILTER, &value, 1U);
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
    if (externalClock && ((this->m_configuration.interruptMap & 0xF0U) != 0U)) {
        return Status::INVALID_CONFIGURATION;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    const std::uint8_t value = static_cast<std::uint8_t>(
        (externalClock ? Registers::SYNC_EXTERNAL_CLOCK : 0U) | static_cast<std::uint8_t>(syncMode));
    status = this->writeRegister(Registers::SYNC, &value, 1U);
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
    status = this->writeRegister(Registers::OFFSET_X_H, values, 6U);
    if (status == Status::OK) {
        std::uint8_t actual[6] = {0U};
        status = this->readRegister(Registers::OFFSET_X_H, actual, 6U);
        for (std::size_t index = 0U; (status == Status::OK) && (index < 6U); ++index) {
            if (actual[index] != values[index]) {
                status = Status::VERIFY_ERROR;
            }
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
    status = this->writeRegister(Registers::ACT_EN, values, 4U);
    if (status == Status::OK) {
        std::uint8_t actual[4] = {0U};
        status = this->readRegister(Registers::ACT_EN, actual, 4U);
        for (std::size_t index = 0U; (status == Status::OK) && (index < 4U); ++index) {
            if (actual[index] != values[index]) {
                status = Status::VERIFY_ERROR;
            }
        }
    }
    return this->finishConfigurationChange(resumeMeasurement, status);
}

Status Device::configureInterrupts(std::uint8_t interruptMap, bool activeHigh) {
    if (this->m_configuration.externalClock && ((interruptMap & 0xF0U) != 0U)) {
        return Status::INVALID_CONFIGURATION;
    }
    bool resumeMeasurement = false;
    Status status = this->enterStandbyForConfiguration(resumeMeasurement);
    if (status != Status::OK) {
        return status;
    }
    status = this->writeRegister(Registers::INT_MAP, &interruptMap, 1U);
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
        status = this->writeRegister(Registers::RANGE, &rangeValue, 1U);
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

Status Device::setSelfTest(bool stimulateAxis1, bool stimulateAxis2) {
    const std::uint8_t value = static_cast<std::uint8_t>((stimulateAxis1 ? 0x01U : 0U) |
                                                          (stimulateAxis2 ? 0x02U : 0U));
    Status status = this->writeRegister(Registers::SELF_TEST, &value, 1U);
    if (status == Status::OK) {
        status = this->verifyRegister(Registers::SELF_TEST, value, 0x03U);
    }
    return status;
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
    status = this->writeRegister(Registers::FIFO_SAMPLES, &watermark, 1U);
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
    statusRegister.raw = value;
    statusRegister.dataReady = (value & Registers::STATUS_DATA_READY) != 0U;
    statusRegister.fifoFull = (value & Registers::STATUS_FIFO_FULL) != 0U;
    statusRegister.fifoOverrun = (value & Registers::STATUS_FIFO_OVERRUN) != 0U;
    statusRegister.activity = (value & Registers::STATUS_ACTIVITY) != 0U;
    statusRegister.nvmBusy = (value & Registers::STATUS_NVM_BUSY) != 0U;
    return Status::OK;
}

Status Device::readSample(Sample& sample) {
    const Status status = this->readRawSample(sample.raw);
    if (status != Status::OK) {
        return status;
    }
    const double lsbPerG = Device::sensitivityLsbPerG(this->m_configuration.range);
    sample.xMetersPerSecondSquared = (static_cast<double>(sample.raw.x) / lsbPerG) * STANDARD_GRAVITY;
    sample.yMetersPerSecondSquared = (static_cast<double>(sample.raw.y) / lsbPerG) * STANDARD_GRAVITY;
    sample.zMetersPerSecondSquared = (static_cast<double>(sample.raw.z) / lsbPerG) * STANDARD_GRAVITY;
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

Status Device::readTemperature(Temperature& temperature) {
    std::uint8_t bytes[2] = {0U, 0U};
    const Status status = this->readRegister(Registers::TEMP2, bytes, 2U);
    if (status != Status::OK) {
        return status;
    }
    temperature.raw = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0] & 0x0FU) << 8U) | static_cast<std::uint16_t>(bytes[1]));
    temperature.celsius =
        ((static_cast<double>(temperature.raw) - 1885.0) / -9.05) + 25.0;
    return Status::OK;
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
    if ((entryCount % 3U) != 0U) {
        return Status::FIFO_MISALIGNED;
    }
    const std::size_t availableSamples = entryCount / 3U;
    const std::size_t samplesToRead = std::min(availableSamples, capacity);
    if (samplesToRead == 0U) {
        return Status::OK;
    }

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
    if ((data == nullptr) || (size == 0U) || (size > Registers::MAX_REGISTER_TRANSFER) || (address > 0x7FU)) {
        return Status::INVALID_ARGUMENT;
    }
    std::uint8_t transmit[Registers::MAX_SPI_TRANSACTION] = {0U};
    std::uint8_t receive[Registers::MAX_SPI_TRANSACTION] = {0U};
    transmit[0] = Registers::readCommand(address);
    if (!this->m_spi.transfer(transmit, receive, size + 1U)) {
        return Status::SPI_ERROR;
    }
    for (std::size_t index = 0U; index < size; ++index) {
        data[index] = receive[index + 1U];
    }
    return Status::OK;
}

Status Device::writeRegister(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    if ((data == nullptr) || (size == 0U) || (size > Registers::MAX_REGISTER_TRANSFER) || (address > 0x7FU)) {
        return Status::INVALID_ARGUMENT;
    }
    std::uint8_t transmit[Registers::MAX_SPI_TRANSACTION] = {0U};
    std::uint8_t receive[Registers::MAX_SPI_TRANSACTION] = {0U};
    transmit[0] = Registers::writeCommand(address);
    for (std::size_t index = 0U; index < size; ++index) {
        transmit[index + 1U] = data[index];
    }
    return this->m_spi.transfer(transmit, receive, size + 1U) ? Status::OK : Status::SPI_ERROR;
}

bool Device::isInitialized() const {
    return this->m_initialized;
}

bool Device::isMeasuring() const {
    return this->m_measuring;
}

Range Device::range() const {
    return this->m_configuration.range;
}

Status Device::validateConfiguration(const Configuration& configuration) const {
    const std::uint8_t rangeValue = static_cast<std::uint8_t>(configuration.range);
    const std::uint8_t odrValue = static_cast<std::uint8_t>(configuration.outputDataRate);
    const std::uint8_t hpfValue = static_cast<std::uint8_t>(configuration.highPassCorner);
    const std::uint8_t syncValue = static_cast<std::uint8_t>(configuration.syncMode);
    if ((rangeValue < 1U) || (rangeValue > 3U) || (odrValue > 10U) || (hpfValue > 6U) ||
        (syncValue > 2U) || (configuration.fifoWatermark < 1U) || (configuration.fifoWatermark > 96U)) {
        return Status::INVALID_CONFIGURATION;
    }
    if (configuration.externalClock && ((configuration.interruptMap & 0xF0U) != 0U)) {
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
    resumeMeasurement = this->m_measuring;
    return resumeMeasurement ? this->stopMeasurement() : Status::OK;
}

Status Device::finishConfigurationChange(bool resumeMeasurement, Status operationStatus) {
    if (operationStatus != Status::OK) {
        // A failed runtime update can leave hardware and software with
        // different register values. Require full initialization before a
        // later START command may re-enter measurement mode.
        this->m_configured = false;
        this->m_initialized = false;
        this->m_measuring = false;
        return operationStatus;
    }
    return resumeMeasurement ? this->startMeasurement() : Status::OK;
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
    switch (rangeValue) {
        case Range::G2:
            return 256000.0;
        case Range::G4:
            return 128000.0;
        case Range::G8:
            return 64000.0;
        default:
            return 256000.0;
    }
}

}  // namespace Adxl355
