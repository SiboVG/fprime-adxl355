#include "TestHarness.hpp"
#include "TestSupport.hpp"

#include "Adxl355/Device/Registers.hpp"

#include <cstddef>
#include <cstdint>

using Adxl355::Configuration;
using Adxl355::Device;
using Adxl355::Identity;
using Adxl355::LifecycleState;
using Adxl355::RawSample;
using Adxl355::Sample;
using Adxl355::Status;
using Adxl355::Temperature;
using Adxl355Test::RecordingDelay;
using Adxl355Test::RegisterSpi;
using Adxl355Test::synchronizedConfiguration;

namespace {

Configuration nonDefaultConfiguration() {
    Configuration configuration;
    configuration.range = Adxl355::Range::G4;
    configuration.outputDataRate = Adxl355::OdrSetting::HZ_1000;
    configuration.highPassCorner = Adxl355::HpfSetting::ODR_X_6_2084E_MINUS_4;
    configuration.interruptMap = 0x05U;
    configuration.fifoWatermark = 30U;
    configuration.syncMode = Adxl355::SyncMode::EXTERNAL_WITH_INTERPOLATION;
    return configuration;
}

}  // namespace

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
    EXPECT_EQ(Status::IDENTITY_MISMATCH, device.initialize(Configuration()));
    EXPECT_EQ(static_cast<std::size_t>(0), spi.resets);
}

ADXL355_TEST(InitializeStoresTheIdentityItAlreadyRead) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[0x03] = 0x02;

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::uint8_t>(0x02), device.identity().revisionId);
    EXPECT_EQ(static_cast<std::size_t>(1), spi.transactionsTo(Adxl355::Registers::DEVID_AD, true));
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
}

ADXL355_TEST(SampleReadIsOneStatusThroughZdataBurst) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.setRawSample(1, 2, 3);

    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    EXPECT_EQ(static_cast<std::size_t>(1), spi.transactions.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x09), spi.transactions.back().transmit[0]);
    EXPECT_EQ(static_cast<std::size_t>(14), spi.transactions.back().transmit.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), device.lastStatusRegister());
}

ADXL355_TEST(StatusCheckedReadReportsStaleDataWithoutReplacingOutput) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.setRawSample(10, 20, 30);

    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    spi.encodeDirect(99, &spi.registers[0x08]);
    EXPECT_EQ(Status::STALE_DATA, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(10), sample.raw.x);

    spi.setRawSample(11, 21, 31);
    EXPECT_EQ(Status::OK, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(11), sample.raw.x);
}

ADXL355_TEST(MissingExternalClockIsStaleRatherThanZeroAcceleration) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    // With EXT_CLK set and no clock the device produces no data (data sheet
    // Rev. D, EXT_SYNC = 01 section): data registers stay zero, DATA_RDY clear.
    Sample sample;
    sample.raw.x = 5;
    EXPECT_EQ(Status::STALE_DATA, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(5), sample.raw.x);
}

ADXL355_TEST(StoppedExternalClockIsFrozenDataAfterTheRepeatLimit) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.setRawSample(10, 20, 30);
    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));

    // The last conversion before the clock stops: afterwards DATA_RDY stays set
    // and every read returns the same axes.
    spi.setRawSample(11, 21, 31);
    spi.clockStopped = true;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    for (std::uint8_t repeat = 1U; repeat < Device::FROZEN_REPEAT_LIMIT; ++repeat) {
        EXPECT_EQ(Status::OK, device.readSample(sample));
    }
    sample.raw.x = 5;
    EXPECT_EQ(Status::FROZEN_DATA, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(5), sample.raw.x);
    EXPECT_EQ(Status::FROZEN_DATA, device.readSample(sample));
    EXPECT_TRUE((device.lastStatusRegister() & 0x01U) != 0U);

    // The clock returns: the next conversion is fresh again, without reinitialization.
    spi.clockStopped = false;
    spi.setRawSample(12, 22, 32);
    EXPECT_EQ(Status::OK, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(12), sample.raw.x);
    EXPECT_EQ(Status::STALE_DATA, device.readSample(sample));
}

ADXL355_TEST(RepeatCountRestartsOnANewSampleAndOnMeasurementStart) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    Sample sample;

    // Identical new conversions count as repeats; stale reads between them
    // neither count nor restart the count.
    spi.setRawSample(1, 2, 3);
    EXPECT_EQ(Status::OK, device.readSample(sample));
    for (std::uint8_t repeat = 1U; repeat < Device::FROZEN_REPEAT_LIMIT; ++repeat) {
        EXPECT_EQ(Status::STALE_DATA, device.readSample(sample));
        spi.setRawSample(1, 2, 3);
        EXPECT_EQ(Status::OK, device.readSample(sample));
    }
    // One axis differing by one LSB restarts the count; then the clock stops.
    spi.setRawSample(1, 2, 4);
    spi.clockStopped = true;
    EXPECT_EQ(Status::OK, device.readSample(sample));
    for (std::uint8_t repeat = 1U; repeat < Device::FROZEN_REPEAT_LIMIT; ++repeat) {
        EXPECT_EQ(Status::OK, device.readSample(sample));
    }

    // A measurement start restarts it too: the first read after it is never a repeat.
    EXPECT_EQ(Status::OK, device.stopMeasurement());
    EXPECT_EQ(Status::OK, device.startMeasurement());
    for (std::uint8_t read = 0U; read < Device::FROZEN_REPEAT_LIMIT; ++read) {
        EXPECT_EQ(Status::OK, device.readSample(sample));
    }
    EXPECT_EQ(Status::FROZEN_DATA, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(4), sample.raw.z);
}

ADXL355_TEST(StuckMisoIsRejectedAsImplausibleOrStale) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.setRawSample(1, 1, 1);

    Sample sample;
    spi.miso = RegisterSpi::Miso::STUCK_HIGH;
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readSample(sample));
    spi.miso = RegisterSpi::Miso::STUCK_LOW;
    EXPECT_EQ(Status::STALE_DATA, device.readSample(sample));
    EXPECT_EQ(static_cast<std::int32_t>(0), sample.raw.x);

    spi.miso = RegisterSpi::Miso::DRIVEN;
    EXPECT_EQ(Status::OK, device.readSample(sample));
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

    EXPECT_EQ(Status::OK, device.setRange(Adxl355::Range::G4));
    spi.setRawSample(128000, 0, 0);
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
    EXPECT_EQ(static_cast<std::size_t>(2), spi.transactionsTo(Adxl355::Registers::TEMP2, true));
}

ADXL355_TEST(TemperatureRereadsAcrossAnUpdateAndRejectsUnstableValues) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);

    Temperature temperature;
    spi.temperatureReads = {0x6FFU, 0x700U, 0x700U};
    EXPECT_EQ(Status::OK, device.readTemperature(temperature));
    EXPECT_EQ(static_cast<std::uint16_t>(0x700), temperature.raw);

    const std::size_t before = spi.transactions.size();
    spi.temperatureReads = {0x700U, 0x7FFU, 0x6FFU};
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    EXPECT_EQ(static_cast<std::uint16_t>(0x700), temperature.raw);
    EXPECT_EQ(static_cast<std::size_t>(3), spi.transactions.size() - before);

    spi.failReadAddress = Adxl355::Registers::TEMP2;
    EXPECT_EQ(Status::SPI_ERROR, device.readTemperature(temperature));
}

ADXL355_TEST(TemperatureRejectsStuckMisoAndValuesOutsideTheStorageRange) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Temperature temperature;
    spi.setTemperature(1885U);
    EXPECT_EQ(Status::OK, device.readTemperature(temperature));

    // All ones: TEMP2's reserved bits are set, rejected on the first read
    // (two agreeing reads would otherwise give raw 4095, about -219 C).
    spi.miso = RegisterSpi::Miso::STUCK_HIGH;
    std::size_t before = spi.transactions.size();
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    EXPECT_EQ(static_cast<std::size_t>(1), spi.transactions.size() - before);
    // All zeros: raw 0, about +233 C.
    spi.miso = RegisterSpi::Miso::STUCK_LOW;
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    spi.miso = RegisterSpi::Miso::DRIVEN;

    spi.registers[Adxl355::Registers::TEMP2] = 0x17U;
    spi.registers[0x07] = 0x5CU;
    before = spi.transactions.size();
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    EXPECT_EQ(static_cast<std::size_t>(1), spi.transactions.size() - before);

    // -55 C to +150 C are accepted (raw 2609 to 754).
    spi.setTemperature(2600U);
    EXPECT_EQ(Status::OK, device.readTemperature(temperature));
    EXPECT_NEAR(-54.0, temperature.celsius, 0.01);
    spi.setTemperature(760U);
    EXPECT_EQ(Status::OK, device.readTemperature(temperature));
    EXPECT_NEAR(149.31, temperature.celsius, 0.01);
    spi.setTemperature(2610U);
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    spi.setTemperature(750U);
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readTemperature(temperature));
    // A rejected read leaves the previous result in place.
    EXPECT_EQ(static_cast<std::uint16_t>(760), temperature.raw);
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
    EXPECT_TRUE(LifecycleState::MEASURING == device.state());
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

ADXL355_TEST(ShadowReferenceIsCapturedBeforeTheFirstSoftwareReset) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    std::size_t firstShadowRead = spi.transactions.size();
    std::size_t firstReset = spi.transactions.size();
    for (std::size_t i = spi.transactions.size(); i > 0U; --i) {
        const std::uint8_t command = spi.transactions[i - 1U].transmit[0];
        if (command == Adxl355::Registers::readCommand(Adxl355::Registers::SHADOW_START)) {
            firstShadowRead = i - 1U;
        }
        if (command == Adxl355::Registers::writeCommand(Adxl355::Registers::RESET)) {
            firstReset = i - 1U;
        }
    }
    EXPECT_TRUE(firstShadowRead < firstReset);

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    // Capture once, then one comparison per reset.
    EXPECT_EQ(static_cast<std::size_t>(3), spi.transactionsTo(Adxl355::Registers::SHADOW_START, true));
}

ADXL355_TEST(SoftResetRepeatsUntilShadowRegistersMatch) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.corruptShadowResets = 1U;

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::size_t>(2), spi.resets);
    EXPECT_TRUE(device.isMeasuring());
}

ADXL355_TEST(PersistentShadowMismatchIsReportedAndLeavesDeviceUnconfigured) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.corruptShadowResets = Device::MAX_RESET_ATTEMPTS;

    EXPECT_EQ(Status::NVM_MISMATCH, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::size_t>(Device::MAX_RESET_ATTEMPTS), spi.resets);
    EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
}

ADXL355_TEST(ReadFailuresBeforeTheResetLeaveTheDeviceUntouched) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.failReadAddress = Adxl355::Registers::SHADOW_START;
    EXPECT_EQ(Status::SPI_ERROR, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::size_t>(0), spi.resets);

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.failReadAddress = Adxl355::Registers::DEVID_AD;
    EXPECT_EQ(Status::SPI_ERROR, device.initialize(synchronizedConfiguration()));
    EXPECT_TRUE(device.isMeasuring());

    spi.failReadAddress = Adxl355::Registers::SHADOW_START;
    EXPECT_EQ(Status::SPI_ERROR, device.softReset());
    EXPECT_FALSE(device.isInitialized());
}

ADXL355_TEST(ResetTransportFailuresLeaveTheDeviceUnconfigured) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    spi.failWriteAddress = Adxl355::Registers::RESET;
    EXPECT_EQ(Status::SPI_ERROR, device.initialize(synchronizedConfiguration()));
    EXPECT_FALSE(device.isInitialized());

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.failReadAddress = Adxl355::Registers::STATUS;
    EXPECT_EQ(Status::SPI_ERROR, device.initialize(synchronizedConfiguration()));
    EXPECT_FALSE(device.isInitialized());
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

ADXL355_TEST(ExternalSyncWithoutExternalClockIsRejectedBeforeIo) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration = synchronizedConfiguration();
    configuration.externalClock = false;

    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    EXPECT_TRUE(spi.transactions.empty());
}

ADXL355_TEST(InvalidConfigurationFieldsAreRejectedBeforeIo) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    Configuration configuration;
    configuration.range = static_cast<Adxl355::Range>(0);
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    configuration = Configuration();
    configuration.outputDataRate = static_cast<Adxl355::OdrSetting>(11);
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    configuration = Configuration();
    configuration.highPassCorner = static_cast<Adxl355::HpfSetting>(7);
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    configuration = Configuration();
    configuration.syncMode = static_cast<Adxl355::SyncMode>(3);
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    configuration = Configuration();
    configuration.fifoWatermark = 0U;
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.initialize(configuration));
    configuration.fifoWatermark = 97U;
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

ADXL355_TEST(StartWriteFailureDuringInitializeLeavesDeviceUnconfigured) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    // POWER_CTL write 1 enters standby for programming; write 2 starts measurement.
    spi.failWriteAddress = Adxl355::Registers::POWER_CTL;
    spi.failWriteOccurrence = 2;

    EXPECT_EQ(Status::SPI_ERROR, device.initialize(synchronizedConfiguration()));
    EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
    EXPECT_EQ(Status::INVALID_STATE, device.startMeasurement());
    EXPECT_FALSE(device.isMeasuring());
}

ADXL355_TEST(EveryProgrammingFailureAfterResetLeavesDeviceUnconfigured) {
    const std::uint8_t writes[] = {Adxl355::Registers::POWER_CTL, Adxl355::Registers::FILTER,
                                   Adxl355::Registers::RANGE,     Adxl355::Registers::FIFO_SAMPLES,
                                   Adxl355::Registers::INT_MAP,   Adxl355::Registers::SYNC};
    for (std::size_t i = 0U; i < sizeof(writes); ++i) {
        RegisterSpi spi;
        RecordingDelay delay;
        Device device(spi, delay);
        spi.failWriteAddress = writes[i];
        EXPECT_EQ(Status::SPI_ERROR, device.initialize(nonDefaultConfiguration()));
        EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
    }
    const std::uint8_t reads[] = {Adxl355::Registers::POWER_CTL, Adxl355::Registers::FILTER,
                                  Adxl355::Registers::RANGE,     Adxl355::Registers::FIFO_SAMPLES,
                                  Adxl355::Registers::INT_MAP,   Adxl355::Registers::SYNC};
    for (std::size_t i = 0U; i < sizeof(reads); ++i) {
        RegisterSpi spi;
        RecordingDelay delay;
        Device device(spi, delay);
        spi.failReadAddress = reads[i];
        EXPECT_EQ(Status::SPI_ERROR, device.initialize(nonDefaultConfiguration()));
        EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
    }
    for (std::size_t i = 1U; i < sizeof(writes); ++i) {
        RegisterSpi spi;
        RecordingDelay delay;
        Device device(spi, delay);
        spi.ignoreWriteAddress = writes[i];
        EXPECT_EQ(Status::VERIFY_ERROR, device.initialize(nonDefaultConfiguration()));
        EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
    }
}

ADXL355_TEST(FailedReconfigurationCannotResumeAPartiallyProgrammedDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    Configuration replacement = synchronizedConfiguration();
    replacement.range = Adxl355::Range::G8;
    spi.failWriteAddress = Adxl355::Registers::FILTER;

    EXPECT_EQ(Status::SPI_ERROR, device.initialize(replacement));
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

ADXL355_TEST(RuntimeControlsRequireAConfiguredDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);

    EXPECT_EQ(Status::INVALID_STATE, device.setRange(Adxl355::Range::G4));
    EXPECT_EQ(Status::INVALID_STATE,
              device.setFilter(Adxl355::OdrSetting::HZ_1000, Adxl355::HpfSetting::OFF));
    EXPECT_EQ(Status::INVALID_STATE, device.setSynchronization(Adxl355::SyncMode::INTERNAL, false));
    EXPECT_EQ(Status::INVALID_STATE, device.setOffsets(1, 2, 3));
    EXPECT_EQ(Status::INVALID_STATE, device.configureActivity(0x01U, 1U, 1U));
    EXPECT_EQ(Status::INVALID_STATE, device.configureInterrupts(0x01U, false));
    EXPECT_EQ(Status::INVALID_STATE, device.setSelfTest(true, false));
    EXPECT_EQ(Status::INVALID_STATE, device.setFifoWatermark(10U));
    EXPECT_TRUE(spi.transactions.empty());
}

ADXL355_TEST(StopIsAllowedFromEveryStateAndStartResumesFromStandby) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);

    EXPECT_EQ(Status::OK, device.stopMeasurement());
    EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(Status::OK, device.stopMeasurement());
    EXPECT_TRUE(LifecycleState::STANDBY == device.state());
    EXPECT_EQ(Status::OK, device.stopMeasurement());
    EXPECT_TRUE(LifecycleState::STANDBY == device.state());

    spi.failWriteAddress = Adxl355::Registers::POWER_CTL;
    EXPECT_EQ(Status::SPI_ERROR, device.startMeasurement());
    EXPECT_TRUE(LifecycleState::STANDBY == device.state());
    EXPECT_EQ(Status::OK, device.startMeasurement());
    EXPECT_TRUE(LifecycleState::MEASURING == device.state());

    spi.failWriteAddress = Adxl355::Registers::POWER_CTL;
    EXPECT_EQ(Status::SPI_ERROR, device.stopMeasurement());
    EXPECT_TRUE(LifecycleState::MEASURING == device.state());
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

ADXL355_TEST(FullFifoIsReadInOneBoundedTransfer) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    for (std::int32_t i = 0; i < 32; ++i) {
        spi.appendFifoSample(i, -i, i * 2);
    }

    RawSample samples[40];
    std::size_t count = 0U;
    EXPECT_EQ(Status::OK, device.readFifo(samples, 40U, count));
    EXPECT_EQ(static_cast<std::size_t>(32), count);
    EXPECT_EQ(static_cast<std::int32_t>(62), samples[31].z);
    EXPECT_EQ(static_cast<std::size_t>(289), spi.transactions.back().transmit.size());

    spi.fifo.clear();
    spi.appendFifoSample(1, 1, 1);
    spi.appendFifoSample(2, 2, 2);
    EXPECT_EQ(Status::OK, device.readFifo(samples, 1U, count));
    EXPECT_EQ(static_cast<std::size_t>(1), count);
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

ADXL355_TEST(FifoReadRejectsEmptyMarkersAndInvalidArguments) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[Adxl355::Registers::FIFO_ENTRIES] = 3U;

    RawSample sample;
    std::size_t count = 0U;
    EXPECT_EQ(Status::FIFO_EMPTY, device.readFifo(&sample, 1U, count));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readFifo(nullptr, 1U, count));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readFifo(&sample, 0U, count));
    spi.registers[Adxl355::Registers::FIFO_ENTRIES] = 0U;
    EXPECT_EQ(Status::OK, device.readFifo(&sample, 1U, count));
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

ADXL355_TEST(FifoReadRejectsAnImplausibleEntryCount) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[Adxl355::Registers::FIFO_ENTRIES] = 126U;

    RawSample sample;
    std::size_t count = 0U;
    EXPECT_EQ(Status::IMPLAUSIBLE_DATA, device.readFifo(&sample, 1U, count));
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

    EXPECT_EQ(Status::INVALID_ARGUMENT, device.setRange(static_cast<Adxl355::Range>(0)));
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              device.setFilter(static_cast<Adxl355::OdrSetting>(11), Adxl355::HpfSetting::OFF));
    EXPECT_EQ(Status::INVALID_ARGUMENT,
              device.setFilter(Adxl355::OdrSetting::HZ_4000, static_cast<Adxl355::HpfSetting>(7)));
}

ADXL355_TEST(RuntimeControlsInStandbyStayInStandby) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(Status::OK, device.stopMeasurement());

    EXPECT_EQ(Status::OK, device.setRange(Adxl355::Range::G8));
    EXPECT_TRUE(LifecycleState::STANDBY == device.state());
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.registers[0x2D] & 0x01U);
}

ADXL355_TEST(SynchronizationControlProgramsAndVerifies) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(Configuration()));

    EXPECT_EQ(Status::OK, device.setSynchronization(Adxl355::SyncMode::EXTERNAL_WITH_INTERPOLATION, false));
    EXPECT_EQ(static_cast<std::uint8_t>(0x02), spi.registers[0x2B]);
    EXPECT_TRUE(device.isMeasuring());

    const std::size_t before = spi.transactions.size();
    EXPECT_EQ(Status::INVALID_CONFIGURATION,
              device.setSynchronization(Adxl355::SyncMode::EXTERNAL_NO_INTERPOLATION, false));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.setSynchronization(static_cast<Adxl355::SyncMode>(3), true));
    EXPECT_EQ(before, spi.transactions.size());

    EXPECT_EQ(Status::OK, device.setSynchronization(Adxl355::SyncMode::EXTERNAL_NO_INTERPOLATION, true));
    EXPECT_EQ(static_cast<std::uint8_t>(0x05), spi.registers[0x2B]);

    spi.ignoreWriteAddress = Adxl355::Registers::SYNC;
    EXPECT_EQ(Status::VERIFY_ERROR, device.setSynchronization(Adxl355::SyncMode::INTERNAL, false));
    EXPECT_FALSE(device.isInitialized());
}

ADXL355_TEST(FifoWatermarkControlProgramsAndVerifies) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    EXPECT_EQ(Status::OK, device.setFifoWatermark(32U));
    EXPECT_EQ(static_cast<std::uint8_t>(32), spi.registers[0x29]);
    EXPECT_TRUE(device.isMeasuring());

    spi.ignoreWriteAddress = Adxl355::Registers::FIFO_SAMPLES;
    EXPECT_EQ(Status::VERIFY_ERROR, device.setFifoWatermark(40U));
    EXPECT_FALSE(device.isInitialized());
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

ADXL355_TEST(OffsetAndActivityReadbackMismatchesInvalidateTheDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.ignoreWriteAddress = Adxl355::Registers::OFFSET_X_H;
    EXPECT_EQ(Status::VERIFY_ERROR, device.setOffsets(1, 2, 3));
    EXPECT_FALSE(device.isInitialized());

    spi.ignoreWriteAddress = -1;
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.ignoreWriteAddress = Adxl355::Registers::ACT_EN;
    EXPECT_EQ(Status::VERIFY_ERROR, device.configureActivity(0x01U, 1U, 1U));
    EXPECT_FALSE(device.isInitialized());
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
    EXPECT_EQ(Status::OK, device.configureInterrupts(0x00U, false));
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x2C] & 0x40U);

    EXPECT_EQ(Status::OK, device.setSelfTest(true, false));
    EXPECT_EQ(static_cast<std::uint8_t>(0x01), spi.registers[0x2E]);
    EXPECT_EQ(Status::OK, device.setSelfTest(true, true));
    EXPECT_EQ(static_cast<std::uint8_t>(0x03), spi.registers[0x2E]);
    EXPECT_EQ(Status::OK, device.setSelfTest(false, false));
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x2E]);
}

ADXL355_TEST(InterruptPolarityReadbackMismatchInvalidatesTheDevice) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(Configuration()));
    spi.ignoreWriteAddress = Adxl355::Registers::RANGE;
    EXPECT_EQ(Status::VERIFY_ERROR, device.configureInterrupts(0x01U, true));
    EXPECT_FALSE(device.isInitialized());
}

ADXL355_TEST(SelfTestStateIsTrackedAndForceRequiresMode) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    const std::size_t before = spi.transactions.size();
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.setSelfTest(false, true));
    EXPECT_EQ(before, spi.transactions.size());
    EXPECT_EQ(Status::OK, device.setSelfTest(true, true));
    EXPECT_EQ(static_cast<std::uint8_t>(0x03), device.selfTestBits());
    EXPECT_TRUE(device.isMeasuring());

    spi.failWriteAddress = Adxl355::Registers::SELF_TEST;
    EXPECT_EQ(Status::SPI_ERROR, device.setSelfTest(false, false));
    EXPECT_FALSE(device.isInitialized());

    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), device.selfTestBits());
    EXPECT_EQ(static_cast<std::uint8_t>(0x00), spi.registers[0x2E]);
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

    EXPECT_EQ(Status::OK, device.setSynchronization(Adxl355::SyncMode::INTERNAL, false));
    EXPECT_EQ(Status::OK, device.configureInterrupts(0x00U, false));
    EXPECT_EQ(Status::OK, device.setSynchronization(Adxl355::SyncMode::EXTERNAL_NO_INTERPOLATION, true));
    EXPECT_EQ(Status::INVALID_CONFIGURATION, device.configureInterrupts(0x10U, false));
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
    EXPECT_EQ(Status::VERIFY_ERROR, device.initialize(Configuration()));
    spi.ignorePowerWrites = false;
    EXPECT_EQ(Status::OK, device.initialize(Configuration()));
    EXPECT_TRUE(device.isMeasuring());
}

ADXL355_TEST(DiagnosticRegisterWriteReturnsTheLifecycleToUnconfigured) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));

    const std::uint8_t value = 0x12U;
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.writeRegister(0x80U, &value, 1U));
    EXPECT_TRUE(device.isMeasuring());
    EXPECT_EQ(Status::OK, device.writeRegister(Adxl355::Registers::OFFSET_X_H, &value, 1U));
    EXPECT_EQ(static_cast<std::uint8_t>(0x12), spi.registers[0x1E]);
    EXPECT_TRUE(LifecycleState::UNCONFIGURED == device.state());
}

ADXL355_TEST(RegisterAccessRejectsInvalidTransfersWithoutIo) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    std::uint8_t data[4] = {0U, 0U, 0U, 0U};

    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readRegister(0x00U, nullptr, 1U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readRegister(0x00U, data, 0U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readRegister(0x00U, data, Adxl355::Registers::MAX_REGISTER_TRANSFER + 1U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.readRegister(0x80U, data, 1U));
    EXPECT_EQ(Status::INVALID_ARGUMENT, device.writeRegister(0x1EU, nullptr, 1U));
    EXPECT_TRUE(spi.transactions.empty());
}

ADXL355_TEST(StatusRegisterFlagsAreDecoded) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    spi.registers[0x04] = 0x1FU;

    Adxl355::StatusRegister status;
    EXPECT_EQ(Status::OK, device.readStatus(status));
    EXPECT_TRUE(status.dataReady && status.fifoFull && status.fifoOverrun && status.activity && status.nvmBusy);
    EXPECT_EQ(static_cast<std::uint8_t>(0x1F), device.lastStatusRegister());

    spi.failReadAddress = Adxl355::Registers::STATUS;
    EXPECT_EQ(Status::SPI_ERROR, device.readStatus(status));
}

ADXL355_TEST(RawDiagnosticReadDoesNotConsumeDataReady) {
    RegisterSpi spi;
    RecordingDelay delay;
    Device device(spi, delay);
    EXPECT_EQ(Status::OK, device.initialize(synchronizedConfiguration()));
    spi.setRawSample(7, 8, 9);

    RawSample raw;
    EXPECT_EQ(Status::OK, device.readRawSample(raw));
    EXPECT_EQ(static_cast<std::int32_t>(7), raw.x);
    Sample sample;
    EXPECT_EQ(Status::OK, device.readSample(sample));

    spi.failReadAddress = Adxl355::Registers::XDATA3;
    EXPECT_EQ(Status::SPI_ERROR, device.readRawSample(raw));
    spi.failReadAddress = Adxl355::Registers::STATUS;
    EXPECT_EQ(Status::SPI_ERROR, device.readSample(sample));
}

int main() {
    return Adxl355Test::runAll();
}
