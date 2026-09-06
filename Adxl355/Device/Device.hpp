#ifndef ADXL355_DEVICE_DEVICE_HPP
#define ADXL355_DEVICE_DEVICE_HPP

#include <cstddef>
#include <cstdint>

namespace Adxl355 {

enum class Status : std::uint8_t {
    OK = 0,
    PARTIAL,
    INVALID_ARGUMENT,
    INVALID_CONFIGURATION,
    INVALID_STATE,
    SPI_ERROR,
    IDENTITY_MISMATCH,
    TIMEOUT,
    VERIFY_ERROR,
    FIFO_MISALIGNED,
    FIFO_EMPTY,
    CAPACITY_EXCEEDED
};

enum class Range : std::uint8_t { G2 = 1, G4 = 2, G8 = 3 };

enum class OdrSetting : std::uint8_t {
    HZ_4000 = 0,
    HZ_2000 = 1,
    HZ_1000 = 2,
    HZ_500 = 3,
    HZ_250 = 4,
    HZ_125 = 5,
    HZ_62_5 = 6,
    HZ_31_25 = 7,
    HZ_15_625 = 8,
    HZ_7_813 = 9,
    HZ_3_906 = 10
};

enum class HpfSetting : std::uint8_t {
    OFF = 0,
    ODR_X_24_7E_MINUS_4 = 1,
    ODR_X_6_2084E_MINUS_4 = 2,
    ODR_X_1_5545E_MINUS_4 = 3,
    ODR_X_0_3862E_MINUS_4 = 4,
    ODR_X_0_0954E_MINUS_4 = 5,
    ODR_X_0_0238E_MINUS_4 = 6
};

enum class SyncMode : std::uint8_t {
    INTERNAL = 0,
    EXTERNAL_NO_INTERPOLATION = 1,
    EXTERNAL_WITH_INTERPOLATION = 2
};

struct Configuration {
    Configuration();

    static Configuration externalSynchronized4kHz();

    Range range;
    OdrSetting outputDataRate;
    HpfSetting highPassCorner;
    SyncMode syncMode;
    bool externalClock;
    bool temperatureEnabled;
    bool dataReadyEnabled;
    bool interruptActiveHigh;
    std::uint8_t interruptMap;
    std::uint8_t fifoWatermark;
};

struct Identity {
    Identity();
    std::uint8_t analogDevicesId;
    std::uint8_t memsId;
    std::uint8_t partId;
    std::uint8_t revisionId;
};

struct StatusRegister {
    StatusRegister();
    bool dataReady;
    bool fifoFull;
    bool fifoOverrun;
    bool activity;
    bool nvmBusy;
    std::uint8_t raw;
};

struct RawSample {
    RawSample();
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
};

struct Sample {
    Sample();
    RawSample raw;
    double xMetersPerSecondSquared;
    double yMetersPerSecondSquared;
    double zMetersPerSecondSquared;
};

struct Temperature {
    Temperature();
    std::uint16_t raw;
    double celsius;
};

class SpiBus {
  public:
    virtual ~SpiBus() {}
    virtual bool transfer(const std::uint8_t* transmit, std::uint8_t* receive, std::size_t size) = 0;
};

class Delay {
  public:
    virtual ~Delay() {}
    virtual void delayMilliseconds(std::uint32_t milliseconds) = 0;
};

class Device {
  public:
    Device(SpiBus& spi, Delay& delay);

    Status initialize(const Configuration& configuration);
    Status readIdentity(Identity& identity);
    Status softReset();
    Status configure(const Configuration& configuration);
    Status startMeasurement();
    Status stopMeasurement();

    Status setRange(Range range);
    Status setFilter(OdrSetting outputDataRate, HpfSetting highPassCorner);
    Status setSynchronization(SyncMode syncMode, bool externalClock);
    Status setOffsets(std::int16_t x, std::int16_t y, std::int16_t z);
    Status configureActivity(std::uint8_t axisMask, std::uint16_t threshold, std::uint8_t count);
    Status configureInterrupts(std::uint8_t interruptMap, bool activeHigh);
    Status setSelfTest(bool stimulateAxis1, bool stimulateAxis2);
    Status setFifoWatermark(std::uint8_t watermark);

    Status readStatus(StatusRegister& status);
    Status readSample(Sample& sample);
    Status readRawSample(RawSample& sample);
    Status readTemperature(Temperature& temperature);
    Status readFifo(RawSample* samples, std::size_t capacity, std::size_t& count);

    Status readRegister(std::uint8_t address, std::uint8_t* data, std::size_t size);
    Status writeRegister(std::uint8_t address, const std::uint8_t* data, std::size_t size);

    bool isInitialized() const;
    bool isMeasuring() const;
    Range range() const;

  private:
    Status validateConfiguration(const Configuration& configuration) const;
    Status verifyRegister(std::uint8_t address, std::uint8_t expected, std::uint8_t mask);
    Status enterStandbyForConfiguration(bool& resumeMeasurement);
    Status finishConfigurationChange(bool resumeMeasurement, Status operationStatus);
    std::uint8_t powerControlValue(bool standby) const;
    static std::int32_t decodeAcceleration(const std::uint8_t* bytes);
    static double sensitivityLsbPerG(Range range);

    SpiBus& m_spi;
    Delay& m_delay;
    Configuration m_configuration;
    bool m_configured;
    bool m_initialized;
    bool m_measuring;
};

}  // namespace Adxl355

#endif
