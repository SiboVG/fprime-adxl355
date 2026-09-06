#include "TestHarness.hpp"
#include "TestSupport.hpp"

#include "Adxl355/Device/Registers.hpp"

#include <cstddef>
#include <cstdint>

using Adxl355::Configuration;
using Adxl355::Device;
using Adxl355::Identity;
using Adxl355::RawSample;
using Adxl355::Sample;
using Adxl355::Status;
using Adxl355::Temperature;
using Adxl355Test::RecordingDelay;
using Adxl355Test::RegisterSpi;
using Adxl355Test::synchronizedConfiguration;

ADXL355_TEST(IdentityReadUsesOneAutoIncrementingSpiTransaction) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Identity identity;

    EXPECT_EQ(Status::OK, device.readIdentity(identity));
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), identity.analogDevicesId);
    EXPECT_EQ(static_cast<std::uint8_t>(0x1D), identity.memsId);
    EXPECT_EQ(static_cast<std::uint8_t>(0xED), identity.partId);
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), identity.revisionId);
    EXPECT_EQ(static_cast<std::size_t>(1), spi.transactions.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.transactions[0].transmit[0]);
    EXPECT_EQ(static_cast<std::size_t>(5), spi.transactions[0].transmit.size());
}

ADXL355_TEST(IdentityMismatchIsRejected) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[0x02] = 0x00;

    Identity identity;
    EXPECT_EQ(Status::IDENTITY_MISMATCH, device.readIdentity(identity));
}

ADXL355_TEST(DirectReadDecodesSignedTwentyBitAxesAndScale) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.setRawSample(256000, -256000, -1);

    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(256000), sample.raw.x);
    EXPECT_EQ(static_cast<std::int32_t>(-256000), sample.raw.y);
    EXPECT_EQ(static_cast<std::int32_t>(-1), sample.raw.z);
    EXPECT_NEAR(9.80665, sample.xMetersPerSecondSquared, 0.0001);
    EXPECT_NEAR(-9.80665, sample.yMetersPerSecondSquared, 0.0001);
    EXPECT_EQ(static_cast<std::uint8_t>(0x11), spi.transactions.back().transmit[0]);
    EXPECT_EQ(static_cast<std::size_t>(10), spi.transactions.back().transmit.size());
}

ADXL355_TEST(ScaleTracksConfiguredRange) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration = synchronizedConfiguration();
    configuration.range = Adxl355::Range::G8;

    EXPECT_EQ(Status::OK, device.initialize(configuration));
    spi.setRawSample(64000, 0, 0);
    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    EXPECT_NEAR(9.80665, sample.xMetersPerSecondSquared, 0.0001);
}

ADXL355_TEST(TemperatureUsesDatasheetInterceptAndSlope) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.setTemperature(1885U);

    Temperature temperature;
    EXPECT_EQ(Status::OK, device.readTemperature(temperature));
    EXPECT_EQ(static_cast<std::uint16_t>(1885), temperature.raw);
    EXPECT_NEAR(25.0, temperature.celsius, 0.0001);
}

ADXL355_TEST(SynchronizedPresetProgramsBoardModeBeforeMeasurement) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.nvmBusyReads = 2U;

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x28]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.registers[0x2C] & 0x03U);
    EXPECT_EQ(static_cast<std::uint8_t>(0x05), spi.registers[0x2B]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x2A] & 0xF0U);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x2D] & 0x01U);
    EXPECT_TRUE(delay.calls.size() >= 3U);
    EXPECT_TRUE(device.isMeasuring());
}

ADXL355_TEST(ResetPollingHasABoundedNvmBusyTimeout) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.nvmBusyReads = 300U;
    delay.calls.clear();

    EXPECT_EQ(Status::TIMEOUT, device.softReset());
    EXPECT_FALSE(device.isInitialized());
    EXPECT_FALSE(device.isMeasuring());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
    EXPECT_EQ(static_cast<std::size_t>(256), delay.calls.size());
}

ADXL355_TEST(ExternalClockRejectsInt2InterruptMappings) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration = synchronizedConfiguration();
    configuration.interruptMap = 0x10U;

    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    EXPECT_TRUE(spi.transactions.empty());
}

ADXL355_TEST(SpiFailureIsReportedWithoutChangingState) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.failNext = true;

    Identity identity;
    EXPECT_EQ(Status::SPI_ERROR, device.readIdentity(identity));
    EXPECT_FALSE(device.isInitialized());
}

ADXL355_TEST(FailedReconfigurationCannotResumeAPartiallyProgrammedDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    Configuration replacement = synchronizedConfiguration();
    replacement.range = Adxl355::Range::G8;
    spi.failWriteAddress = Adxl355::Registers::FILTER;

    EXPECT_EQ(Status::SPI_ERROR, device.configure(replacement));
    EXPECT_FALSE(device.isMeasuring());
    EXPECT_FALSE(device.isInitialized());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
}

ADXL355_TEST(FailedRuntimeReconfigurationInvalidatesDeviceState) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    spi.failWriteAddress = Adxl355::Registers::RANGE;
    EXPECT_EQ(Status::SPI_ERROR, device.setRange(Adxl355::Range::G8));
    EXPECT_FALSE(device.isMeasuring());
    EXPECT_FALSE(device.isInitialized());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
}

ADXL355_TEST(FifoReadReturnsWholeVerifiedXyzSets) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.appendFifoSample(1, -2, 3);
    spi.appendFifoSample(-4, 5, -6);

    RawSample samples[2];
    std::size_t count = 0U;
    EXPECT_EQ(Status::OK, device.readFifo(samples, 2U, count));
    EXPECT_EQ(static_cast<std::size_t>(2), count);
    EXPECT_EQ(static_cast<std::int32_t>(1), samples[0].x);
    EXPECT_EQ(static_cast<std::int32_t>(-2), samples[0].y);
    EXPECT_EQ(static_cast<std::int32_t>(-6), samples[1].z);
}

ADXL355_TEST(FifoReadRejectsBrokenAxisMarkers) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.appendFifoSample(1, 2, 3);
    spi.fifo[2] = static_cast<std::uint8_t>(spi.fifo[2] & ~0x01U);

    RawSample sample;
    std::size_t count = 0U;
    EXPECT_EQ(Status::FIFO_MISALIGNED, device.readFifo(&sample, 1U, count));
    EXPECT_EQ(static_cast<std::size_t>(0), count);
}

ADXL355_TEST(FifoReadRejectsAPartialXyzEntryCount) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[Adxl355::Registers::FIFO_ENTRIES] = 2U;

    RawSample sample;
    std::size_t count = 0U;
    EXPECT_EQ(Status::FIFO_MISALIGNED, device.readFifo(&sample, 1U, count));
    EXPECT_EQ(static_cast<std::size_t>(0), count);
}

ADXL355_TEST(RuntimeRangeAndFilterChangesUseStandbyAndResumeMeasurement) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    const std::size_t before = spi.transactions.size();

    EXPECT_EQ(Status::OK, device.setRange(Adxl355::Range::G4));
    EXPECT_EQ(Status::OK,
              device.setFilter(Adxl355::OdrSetting::HZ_1000,
                               Adxl355::HpfSetting::ODR_X_6_2084E_MINUS_4));

    EXPECT_TRUE(device.isMeasuring());
    EXPECT_EQ(Adxl355::Range::G4, device.range());
    EXPECT_EQ(static_cast<std::uint8_t>(0x02), spi.registers[0x2C] & 0x03U);
    EXPECT_EQ(static_cast<std::uint8_t>(0x22), spi.registers[0x28]);
    EXPECT_TRUE(spi.transactions.size() > before);
    bool observedStandby = false;
    for (std::size_t index = before; index < spi.transactions.size(); ++index) {
        const Adxl355Test::Transaction& transaction = spi.transactions[index];
        if ((transaction.transmit[0] == 0x5AU) && (transaction.transmit.size() == 2U) &&
            ((transaction.transmit[1] & 0x01U) != 0U)) {
            observedStandby = true;
        }
    }
    EXPECT_TRUE(observedStandby);
}

ADXL355_TEST(OffsetsAndActivityConfigurationAreWrittenBigEndian) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    EXPECT_EQ(Status::OK, device.setOffsets(-1, 0x1234, -32768));
    EXPECT_EQ(static_cast<std::uint8_t>(0xFF), spi.registers[0x1E]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xFF), spi.registers[0x1F]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x12), spi.registers[0x20]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x34), spi.registers[0x21]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x80), spi.registers[0x22]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x23]);

    EXPECT_EQ(Status::OK, device.configureActivity(0x05U, 0x3456U, 7U));
    EXPECT_EQ(static_cast<std::uint8_t>(0x05), spi.registers[0x24]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x34), spi.registers[0x25]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x56), spi.registers[0x26]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x07), spi.registers[0x27]);
    EXPECT_TRUE(device.isMeasuring());
}

ADXL355_TEST(InterruptAndSelfTestControlPreserveUnrelatedBits) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration;
    EXPECT_EQ(Status::OK, device.initialize(configuration));

    EXPECT_EQ(Status::OK, device.configureInterrupts(0x0BU, true));
    EXPECT_EQ(static_cast<std::uint8_t>(0x0B), spi.registers[0x2A]);
    EXPECT_EQ(static_cast<std::uint8_t>(0x40), spi.registers[0x2C] & 0x40U);
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.registers[0x2C] & 0x03U);

    EXPECT_EQ(Status::OK, device.setSelfTest(true, false));
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.registers[0x2E]);
    EXPECT_EQ(Status::OK, device.setSelfTest(false, true));
    EXPECT_EQ(static_cast<std::uint8_t>(0x02), spi.registers[0x2E]);
}

ADXL355_TEST(SynchronizationControlRejectsClockAndInt2ConflictBeforeIo) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration;
    configuration.interruptMap = 0x10U;
    EXPECT_EQ(Status::OK, device.initialize(configuration));
    const std::size_t before = spi.transactions.size();

    EXPECT_EQ(Status::INVALID_CONFIGURATION,
              device.setSynchronization(Adxl355::SyncMode::EXTERNAL_NO_INTERPOLATION, true));
    EXPECT_EQ(before, spi.transactions.size());
    EXPECT_TRUE(device.isMeasuring());
}

ADXL355_TEST(ControlArgumentValidationDoesNotTouchTheBus) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    const std::size_t before = spi.transactions.size();

    EXPECT_EQ(Status::INVALID_ARGUMENT, device.setFifoWatermark(0U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.setFifoWatermark(97U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.configureActivity(0x08U, 1U, 1U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.configureActivity(0x01U, 1U, 0U));
    EXPECT_EQ(before, spi.transactions.size());
}

ADXL355_TEST(IgnoredMeasurementStartCannotInitializeDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.ignorePowerWrites = true;
    EXPECT_EQ(Status::VERIFY_ERROR, device.initialize(Configuration()));
    EXPECT_FALSE(device.isInitialized());
    EXPECT_FALSE(device.isMeasuring());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
}

ADXL355_TEST(IgnoredStandbyBlocksRuntimeConfiguration) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(Configuration()));
    spi.ignorePowerWrites = true;
    const auto before = spi.registers[0x2C];
    EXPECT_EQ(Status::VERIFY_ERROR, device.setRange(Adxl355::Range::G8));
    EXPECT_EQ(before, spi.registers[0x2C]);
    EXPECT_FALSE(device.isInitialized());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
    EXPECT_EQ(Status::VERIFY_ERROR, device.configure(Configuration()));
    spi.ignorePowerWrites = false;
    EXPECT_EQ(Status::OK, device.initialize(Configuration()));
    EXPECT_TRUE(device.isMeasuring());
}

int main() {
    return Adxl355Test::runAll();
}
