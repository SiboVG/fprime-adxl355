#include "ManagerTester.hpp"

#include "Adxl355/Device/Registers.hpp"

namespace Adxl355 {

namespace {

DeviceConfiguration synchronizedPreset() {
    return DeviceConfiguration(SensorRange::G2,
                               OutputDataRate::HZ_4000,
                               HighPassCorner::OFF,
                               SynchronizationMode::EXTERNAL_NO_INTERPOLATION,
                               true,
                               true,
                               true,
                               false,
                               0U,
                               96U);
}

}  // namespace

ManagerTester::ManagerTester()
    : ManagerGTestBase("ManagerTester", ManagerTester::MAX_HISTORY_SIZE),
      m_emulator(),
      m_reentry(Reentry::NONE),
      m_reentrantStatus(DriverStatus::OK),
      m_reentrantPortStatus(),
      m_driverStatus(Drv::SpiStatus::SPI_OK),
      m_shortenReadBuffer(false),
      component("Manager") {
    this->initComponents();
    this->connectPorts();
}

ManagerTester::~ManagerTester() {}

void ManagerTester::initialize() {
    this->clearHistory();
    this->sendCmd_INITIALIZE_SYNCED(0, 1U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_INITIALIZE_SYNCED, 1U, Fw::CmdResponse::OK);
    this->clearHistory();
}

void ManagerTester::testConfigureAndRead() {
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_configure(0, synchronizedPreset()));
    ASSERT_EVENTS_InitializationComplete_SIZE(1);
    ASSERT_EVENTS_InitializationComplete(0, 0x01U);
    ASSERT_TLM_Initialized(0, 1U);
    ASSERT_TLM_Measuring(0, 1U);
    ASSERT_EQ(0x05U, this->m_emulator.registers[0x2B]);
    // The revision comes from the identity read initialize() already made.
    ASSERT_EQ(1U, this->m_emulator.transactionsTo(Registers::DEVID_AD, true));

    this->m_emulator.setRawSample(256000, -256000, 0);
    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(256000, sample.get_rawX());
    EXPECT_EQ(-256000, sample.get_rawY());
    EXPECT_NEAR(9.80665, sample.get_xMetersPerSecondSquared(), 0.0001);
    ASSERT_TLM_RawX_SIZE(0);
    ASSERT_TLM_SamplesRead_SIZE(0);

    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_TLM_RawX(0, 256000);
    ASSERT_TLM_SamplesRead(0, 2U);
    ASSERT_TLM_StatusRegister(0, 0x01U);
}

void ManagerTester::testControlCommands() {
    this->initialize();

    this->sendCmd_SET_RANGE(0, 11U, SensorRange::G8);
    this->sendCmd_SET_FILTER(0, 12U, OutputDataRate::HZ_1000, HighPassCorner::ODR_X_6_2084E_MINUS_4);
    this->sendCmd_SET_SYNC(0, 13U, SynchronizationMode::EXTERNAL_WITH_INTERPOLATION, false);
    this->sendCmd_SET_SYNC(0, 14U, SynchronizationMode::EXTERNAL_NO_INTERPOLATION, true);
    this->sendCmd_SET_OFFSETS(0, 15U, -1, 2, 3);
    this->sendCmd_SET_ACTIVITY(0, 16U, 0x07U, 0x0100U, 3U);
    this->sendCmd_SET_INTERRUPTS(0, 17U, 0x0FU, true);
    this->sendCmd_SET_FIFO_WATERMARK(0, 18U, 40U);
    this->sendCmd_SET_SELF_TEST(0, 19U, true, true);
    this->sendCmd_STOP(0, 20U);
    ASSERT_TLM_Measuring(this->tlmHistory_Measuring->size() - 1U, 0U);
    this->sendCmd_START(0, 21U);

    ASSERT_CMD_RESPONSE_SIZE(11);
    for (U32 index = 0U; index < 11U; ++index) {
        ASSERT_EQ(Fw::CmdResponse::OK, this->cmdResponseHistory->at(index).response) << index;
    }
    ASSERT_EQ(0x03U, this->m_emulator.registers[0x2C] & 0x03U);
    ASSERT_EQ(0x40U, this->m_emulator.registers[0x2C] & 0x40U);
    ASSERT_EQ(0x22U, this->m_emulator.registers[0x28]);
    ASSERT_EQ(0x05U, this->m_emulator.registers[0x2B]);
    ASSERT_EQ(0xFFU, this->m_emulator.registers[0x1E]);
    ASSERT_EQ(0x03U, this->m_emulator.registers[0x23]);
    ASSERT_EQ(0x07U, this->m_emulator.registers[0x24]);
    ASSERT_EQ(0x03U, this->m_emulator.registers[0x27]);
    ASSERT_EQ(0x0FU, this->m_emulator.registers[0x2A]);
    ASSERT_EQ(40U, this->m_emulator.registers[0x29]);
    ASSERT_EQ(0x03U, this->m_emulator.registers[0x2E]);
    ASSERT_EQ(0x00U, this->m_emulator.registers[0x2D] & 0x01U);
    ASSERT_EVENTS_SelfTestState_SIZE(1);
    ASSERT_EVENTS_SelfTestState(0, true, true);
    ASSERT_TLM_SelfTest(this->tlmHistory_SelfTest->size() - 1U, 0x03U);
    ASSERT_TLM_Measuring(this->tlmHistory_Measuring->size() - 1U, 1U);
    ASSERT_EVENTS_OperationFailed_SIZE(0);
}

void ManagerTester::testCommandValidationErrorsAreNotDeviceFaults() {
    this->sendCmd_SET_RANGE(0, 1U, SensorRange::G4);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_SET_RANGE, 1U, Fw::CmdResponse::EXECUTION_ERROR);
    this->sendCmd_START(0, 2U);
    ASSERT_CMD_RESPONSE(1, Manager::OPCODE_START, 2U, Fw::CmdResponse::EXECUTION_ERROR);
    this->initialize();

    this->sendCmd_SET_FIFO_WATERMARK(0, 3U, 0U);
    this->sendCmd_SET_ACTIVITY(0, 4U, 0x08U, 1U, 1U);
    this->sendCmd_SET_SELF_TEST(0, 5U, false, true);
    this->sendCmd_SET_SYNC(0, 6U, SynchronizationMode::EXTERNAL_NO_INTERPOLATION, false);
    this->sendCmd_SET_INTERRUPTS(0, 7U, 0x10U, false);
    ASSERT_CMD_RESPONSE_SIZE(5);
    for (U32 index = 0U; index < 5U; ++index) {
        ASSERT_EQ(Fw::CmdResponse::VALIDATION_ERROR, this->cmdResponseHistory->at(index).response) << index;
    }
    ASSERT_EVENTS_OperationFailed_SIZE(0);
    ASSERT_TLM_Errors_SIZE(0);

    this->m_emulator.setRawSample(1, 2, 3);
    this->sendCmd_READ_SNAPSHOT(0, 8U);
    ASSERT_TLM_Errors(0, 0U);
    ASSERT_TLM_Measuring(this->tlmHistory_Measuring->size() - 1U, 1U);
}

void ManagerTester::testHighRateFailuresAreEdgeReportedAndThrottled() {
    this->initialize();
    AccelerationSample sample;

    // Repeated equal failures are counted but queued once. The read itself
    // emits nothing; schedIn publishes the event, LastStatus and Errors.
    EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    ASSERT_EVENTS_OperationFailed_SIZE(0);
    ASSERT_TLM_LastStatus_SIZE(0);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::STALE_DATA);
    ASSERT_TLM_LastStatus(0, DriverStatus::STALE_DATA);
    ASSERT_TLM_Errors(0, 2U);
    // Without new edges, schedIn publishes nothing.
    this->invoke_to_schedIn(0, 0U);
    ASSERT_TLM_LastStatus_SIZE(1);

    // An intermittent sensor produces one edge per failure, bounded by the throttle.
    for (U32 cycle = 0U; cycle < 30U; ++cycle) {
        this->m_emulator.setRawSample(static_cast<I32>(cycle), 0, 0);
        EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
        EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
        if ((cycle % 4U) == 3U) {
            this->invoke_to_schedIn(0, 0U);  // eight edges per call fit the queue
        }
    }
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(10);

    this->sendCmd_CLEAR_EVENT_THROTTLE(0, 5U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_CLEAR_EVENT_THROTTLE, 5U, Fw::CmdResponse::OK);
    this->m_emulator.setRawSample(1, 0, 0);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(11);

    this->m_emulator.setRawSample(2, 0, 0);
    this->sendCmd_READ_SNAPSHOT(0, 6U);
    ASSERT_TLM_Errors(this->tlmHistory_Errors->size() - 1U, 33U);
}

void ManagerTester::testReadBeforeInitializationIsRejected() {
    AccelerationSample sample;
    ASSERT_EQ(DriverStatus::INVALID_STATE, this->invoke_to_readAcceleration(0, sample));
    ASSERT_TRUE(this->m_emulator.transactions.empty());
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::INVALID_STATE);

    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_TRUE(this->m_emulator.transactions.empty());
}

void ManagerTester::testImplausibleBusDataIsRejected() {
    this->m_emulator.miso = Adxl355Test::RegisterSpi::Miso::STUCK_HIGH;
    EXPECT_EQ(DriverStatus::IDENTITY_MISMATCH, this->invoke_to_configure(0, synchronizedPreset()));
    ASSERT_TLM_Initialized(0, 0U);
    ASSERT_EVENTS_OperationFailed(0, Operation::INITIALIZE, DriverStatus::IDENTITY_MISMATCH);

    this->m_emulator.miso = Adxl355Test::RegisterSpi::Miso::DRIVEN;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_configure(0, synchronizedPreset()));
    this->m_emulator.setRawSample(1, 1, 1);
    AccelerationSample sample;
    TemperatureSample temperature;
    this->m_emulator.miso = Adxl355Test::RegisterSpi::Miso::STUCK_HIGH;
    EXPECT_EQ(DriverStatus::IMPLAUSIBLE_DATA, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(DriverStatus::IMPLAUSIBLE_DATA, this->invoke_to_readTemperature(0, temperature));
    this->m_emulator.miso = Adxl355Test::RegisterSpi::Miso::STUCK_LOW;
    EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(DriverStatus::IMPLAUSIBLE_DATA, this->invoke_to_readTemperature(0, temperature));
    this->m_emulator.miso = Adxl355Test::RegisterSpi::Miso::DRIVEN;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
}

void ManagerTester::testStoppedClockIsReportedAsFrozenData() {
    this->initialize();
    AccelerationSample sample;

    // The external clock stops after a conversion: DATA_RDY stays set and every
    // read returns the same axes, which pass until the repeat limit.
    this->m_emulator.setRawSample(4, 5, 6);
    this->m_emulator.clockStopped = true;
    for (U32 read = 0U; read < Device::FROZEN_REPEAT_LIMIT; ++read) {
        EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    }
    EXPECT_EQ(DriverStatus::FROZEN_DATA, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(DriverStatus::FROZEN_DATA, this->invoke_to_readAcceleration(0, sample));
    ASSERT_EVENTS_OperationFailed_SIZE(0);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::FROZEN_DATA);
    ASSERT_TLM_LastStatus(0, DriverStatus::FROZEN_DATA);
    ASSERT_TLM_Errors(0, 2U);

    // The clock returns: the sensor recovers without reinitialization.
    this->m_emulator.clockStopped = false;
    this->m_emulator.setRawSample(7, 8, 9);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(7, sample.get_rawX());
    this->invoke_to_schedIn(0, 0U);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1U, DriverStatus::OK);
}

void ManagerTester::testHighRateReadNeverWaitsForAControlOperation() {
    this->initialize();
    this->m_emulator.setRawSample(5, 5, 5);

    // READ_SNAPSHOT owns the gate during its SPI transfers; a read arriving
    // then must return BUSY immediately rather than block.
    this->m_reentry = Reentry::READ_ACCELERATION;
    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_EQ(DriverStatus::BUSY, this->m_reentrantStatus);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_OperationFailed_SIZE(0);

    // A control command arriving meanwhile waits a bounded time, then reports.
    this->m_reentry = Reentry::STOP_COMMAND;
    this->sendCmd_READ_SNAPSHOT(0, 2U);
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(1, Manager::OPCODE_STOP, 9U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ControlBusy_SIZE(1);
    ASSERT_EVENTS_ControlBusy(0, Operation::STOP);

    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
}

void ManagerTester::testFailedInitializationReportsConsistentLifecycle() {
    this->m_emulator.ignorePowerWrites = true;
    this->sendCmd_INITIALIZE_SYNCED(0, 1U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_INITIALIZE_SYNCED, 1U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_TLM_Initialized(0, 0U);
    ASSERT_TLM_Measuring(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::INITIALIZE, DriverStatus::VERIFY_ERROR);

    this->sendCmd_START(0, 2U);
    ASSERT_CMD_RESPONSE(1, Manager::OPCODE_START, 2U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
}

void ManagerTester::testTemperatureAndMeasurementControlPorts() {
    this->m_emulator.setTemperature(1885U);
    TemperatureSample temperature;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readTemperature(0, temperature));
    EXPECT_NEAR(25.0, temperature.get_celsius(), 0.0001);
    this->m_emulator.temperatureReads = {0x700U, 0x7FFU, 0x6FFU};
    EXPECT_EQ(DriverStatus::IMPLAUSIBLE_DATA, this->invoke_to_readTemperature(0, temperature));
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_TEMPERATURE, DriverStatus::IMPLAUSIBLE_DATA);

    EXPECT_EQ(DriverStatus::INVALID_STATE, this->invoke_to_measurementControl(0, true));
    this->initialize();
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_measurementControl(0, false));
    ASSERT_EVENTS_MeasurementState(0, false);
    ASSERT_TLM_Measuring(0, 0U);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_measurementControl(0, true));
    ASSERT_TLM_Measuring(1, 1U);
}

void ManagerTester::testSnapshotDoesNotConsumeDataReady() {
    this->initialize();
    this->m_emulator.setRawSample(7, 8, 9);
    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_TLM_RawX(0, 7);

    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(7, sample.get_rawX());
}

void ManagerTester::testNvmMismatchIsReported() {
    this->m_emulator.corruptShadowResets = Device::MAX_RESET_ATTEMPTS;
    EXPECT_EQ(DriverStatus::NVM_MISMATCH, this->invoke_to_configure(0, synchronizedPreset()));
    ASSERT_EVENTS_OperationFailed(0, Operation::INITIALIZE, DriverStatus::NVM_MISMATCH);
    ASSERT_TLM_Initialized(0, 0U);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_configure(0, synchronizedPreset()));
}

void ManagerTester::testFailedSelfTestInvalidatesTheDevice() {
    this->initialize();
    this->m_emulator.ignoreWriteAddress = Registers::SELF_TEST;
    this->sendCmd_SET_SELF_TEST(0, 1U, true, false);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_SET_SELF_TEST, 1U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_OperationFailed(0, Operation::SET_SELF_TEST, DriverStatus::VERIFY_ERROR);
    ASSERT_TLM_Initialized(0, 0U);
    ASSERT_EVENTS_SelfTestState_SIZE(0);
}

void ManagerTester::testEveryControlPathReportsBusyWithoutBlocking() {
    this->initialize();
    this->m_emulator.setRawSample(1, 2, 3);
    this->m_reentry = Reentry::EVERY_CONTROL;
    this->sendCmd_READ_SNAPSHOT(0, 1U);

    // Every command waited a bounded time, reported, and changed nothing.
    ASSERT_CMD_RESPONSE_SIZE(15);
    for (U32 index = 0U; index < 14U; ++index) {
        ASSERT_EQ(Fw::CmdResponse::EXECUTION_ERROR, this->cmdResponseHistory->at(index).response) << index;
    }
    ASSERT_CMD_RESPONSE(14, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_ControlBusy_SIZE(17);
    for (U32 index = 0U; index < 3U; ++index) {
        ASSERT_EQ(DriverStatus::BUSY, this->m_reentrantPortStatus[index]) << index;
    }
    ASSERT_EVENTS_OperationFailed_SIZE(0);
    ASSERT_EQ(0x00U, this->m_emulator.registers[0x2E]);
    ASSERT_EQ(0x00U, this->m_emulator.registers[0x2D] & 0x01U);

    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
}

void ManagerTester::testDriverFailuresPropagateThroughTheSpiPort() {
    // Initialization stops at the first failed transfer.
    this->m_driverStatus = Drv::SpiStatus::SPI_OTHER_ERR;
    EXPECT_EQ(DriverStatus::SPI_ERROR, this->invoke_to_configure(0, synchronizedPreset()));
    ASSERT_EVENTS_OperationFailed(0, Operation::INITIALIZE, DriverStatus::SPI_ERROR);
    ASSERT_EQ(1U, this->m_emulator.transactions.size());

    this->m_driverStatus = Drv::SpiStatus::SPI_OK;
    this->initialize();
    this->m_emulator.setRawSample(1, 2, 3);
    AccelerationSample sample;
    this->m_driverStatus = Drv::SpiStatus::SPI_MISMATCH_ERR;
    EXPECT_EQ(DriverStatus::SPI_ERROR, this->invoke_to_readAcceleration(0, sample));
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::SPI_ERROR);

    // A driver that reports success but changes a buffer is also a failure.
    this->m_driverStatus = Drv::SpiStatus::SPI_OK;
    this->m_shortenReadBuffer = true;
    EXPECT_EQ(DriverStatus::SPI_ERROR, this->invoke_to_readAcceleration(0, sample));
    this->m_shortenReadBuffer = false;
    // That transfer reached the emulator and consumed DATA_RDY, so post a new sample.
    this->m_emulator.setRawSample(1, 2, 3);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(1, sample.get_rawX());
}

void ManagerTester::testHighRateEdgesBeyondTheQueueAreDropped() {
    this->initialize();
    AccelerationSample sample;
    // Twelve fresh-then-stale pairs without schedIn are 23 edges (the first
    // fresh read is no edge); the queue keeps the first sixteen, eight of them
    // failures, below the throttle of ten.
    for (U32 cycle = 0U; cycle < 12U; ++cycle) {
        this->m_emulator.setRawSample(static_cast<I32>(cycle), 0, 0);
        EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
        EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    }
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(8);
    // LastStatus is the current status and Errors counts every failure, also
    // those whose edges were dropped.
    ASSERT_TLM_LastStatus_SIZE(1);
    ASSERT_TLM_LastStatus(0, DriverStatus::STALE_DATA);
    ASSERT_TLM_Errors(0, 12U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(8);
    ASSERT_TLM_LastStatus_SIZE(1);

    // A recovery alone is published as well.
    this->m_emulator.setRawSample(99, 0, 0);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_OperationFailed_SIZE(8);
    ASSERT_TLM_LastStatus(1, DriverStatus::OK);
}

void ManagerTester::testSchedInNeverWaitsForTheGate() {
    this->initialize();
    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::STALE_DATA, this->invoke_to_readAcceleration(0, sample));
    // READ_SNAPSHOT owns the gate during its SPI transfers; schedIn called then
    // publishes at once instead of waiting or reporting busy.
    this->m_reentry = Reentry::SCHED_IN;
    this->m_emulator.setRawSample(1, 2, 3);
    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::STALE_DATA);
    ASSERT_EVENTS_ControlBusy_SIZE(0);
}

Drv::SpiStatus ManagerTester::from_spi_handler(const FwIndexType portNum,
                                               Fw::Buffer& writeBuffer,
                                               Fw::Buffer& readBuffer) {
    (void)portNum;
    EXPECT_EQ(writeBuffer.getSize(), readBuffer.getSize());
    const Reentry reentry = this->m_reentry;
    this->m_reentry = Reentry::NONE;
    if (reentry == Reentry::READ_ACCELERATION) {
        AccelerationSample sample;
        this->m_reentrantStatus = this->invoke_to_readAcceleration(0, sample);
    } else if (reentry == Reentry::SCHED_IN) {
        this->invoke_to_schedIn(0, 0U);
    } else if (reentry == Reentry::STOP_COMMAND) {
        this->sendCmd_STOP(0, 9U);
    } else if (reentry == Reentry::EVERY_CONTROL) {
        this->sendCmd_INITIALIZE_SYNCED(0, 20U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
        this->sendCmd_START(0, 21U);
        this->sendCmd_STOP(0, 22U);
        this->sendCmd_SET_RANGE(0, 23U, SensorRange::G8);
        this->sendCmd_SET_FILTER(0, 24U, OutputDataRate::HZ_1000, HighPassCorner::OFF);
        this->sendCmd_SET_SYNC(0, 25U, SynchronizationMode::INTERNAL, false);
        this->sendCmd_SET_OFFSETS(0, 26U, 1, 2, 3);
        this->sendCmd_SET_ACTIVITY(0, 27U, 0x01U, 1U, 1U);
        this->sendCmd_SET_INTERRUPTS(0, 28U, 0x01U, false);
        this->sendCmd_SET_SELF_TEST(0, 29U, true, false);
        this->sendCmd_SET_FIFO_WATERMARK(0, 30U, 10U);
        this->sendCmd_READ_SNAPSHOT(0, 31U);
        this->sendCmd_CLEAR_EVENT_THROTTLE(0, 32U);
        this->sendCmd_INITIALIZE_SYNCED(0, 33U, SensorRange::G4, OutputDataRate::HZ_4000, HighPassCorner::OFF);
        this->m_reentrantPortStatus[0] = this->invoke_to_configure(0, DeviceConfiguration());
        TemperatureSample temperature;
        this->m_reentrantPortStatus[1] = this->invoke_to_readTemperature(0, temperature);
        this->m_reentrantPortStatus[2] = this->invoke_to_measurementControl(0, false);
    }
    if (this->m_driverStatus != Drv::SpiStatus::SPI_OK) {
        this->m_emulator.transactions.push_back(Adxl355Test::Transaction{
            std::vector<std::uint8_t>(writeBuffer.getData(), writeBuffer.getData() + writeBuffer.getSize())});
        return this->m_driverStatus;
    }
    const bool transferred =
        this->m_emulator.transfer(writeBuffer.getData(), readBuffer.getData(), static_cast<std::size_t>(writeBuffer.getSize()));
    if (this->m_shortenReadBuffer) {
        readBuffer.setSize(readBuffer.getSize() - 1U);
    }
    return transferred ? Drv::SpiStatus::SPI_OK : Drv::SpiStatus::SPI_OTHER_ERR;
}

}  // namespace Adxl355
