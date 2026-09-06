#include "ManagerTester.hpp"

namespace Adxl355 {

ManagerTester::ManagerTester()
    : ManagerGTestBase("ManagerTester", ManagerTester::MAX_HISTORY_SIZE), m_emulator(), component("Manager") {
    this->initComponents();
    this->connectPorts();
}

ManagerTester::~ManagerTester() {}

void ManagerTester::testConfigureAndRead() {
    DeviceConfiguration configuration(SensorRange::G2,
                                      OutputDataRate::HZ_4000,
                                      HighPassCorner::OFF,
                                      SynchronizationMode::EXTERNAL_NO_INTERPOLATION,
                                      true,
                                      true,
                                      true,
                                      false,
                                      0U,
                                      96U);
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_configure(0, configuration));
    ASSERT_EVENTS_InitializationComplete_SIZE(1);
    ASSERT_EVENTS_InitializationComplete(0, 0x01U);
    ASSERT_TLM_Initialized(0, 1U);
    ASSERT_TLM_Measuring(0, 1U);
    ASSERT_EQ(0x05U, this->m_emulator.registers[0x2B]);

    this->m_emulator.setRawSample(256000, -256000, 0);
    AccelerationSample sample;
    EXPECT_EQ(DriverStatus::OK, this->invoke_to_readAcceleration(0, sample));
    EXPECT_EQ(256000, sample.getrawX());
    EXPECT_EQ(-256000, sample.getrawY());
    EXPECT_NEAR(9.80665, sample.getxMetersPerSecondSquared(), 0.0001);
    ASSERT_TLM_RawX_SIZE(0);
    ASSERT_TLM_SamplesRead_SIZE(0);

    this->sendCmd_READ_SNAPSHOT(0, 1U);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_READ_SNAPSHOT, 1U, Fw::CmdResponse::OK);
    ASSERT_TLM_RawX(0, 256000);
    ASSERT_TLM_SamplesRead(0, 2U);
}

void ManagerTester::testControlCommands() {
    this->sendCmd_INITIALIZE_SYNCED(0,
                                    10U,
                                    SensorRange::G2,
                                    OutputDataRate::HZ_4000,
                                    HighPassCorner::OFF);
    ASSERT_CMD_RESPONSE(0, Manager::OPCODE_INITIALIZE_SYNCED, 10U, Fw::CmdResponse::OK);

    this->sendCmd_SET_RANGE(0, 11U, SensorRange::G8);
    ASSERT_CMD_RESPONSE(1, Manager::OPCODE_SET_RANGE, 11U, Fw::CmdResponse::OK);
    ASSERT_EQ(0x03U, this->m_emulator.registers[0x2C] & 0x03U);

    this->sendCmd_STOP(0, 12U);
    ASSERT_CMD_RESPONSE(2, Manager::OPCODE_STOP, 12U, Fw::CmdResponse::OK);
    ASSERT_EQ(0x01U, this->m_emulator.registers[0x2D] & 0x01U);

    this->sendCmd_START(0, 13U);
    ASSERT_CMD_RESPONSE(3, Manager::OPCODE_START, 13U, Fw::CmdResponse::OK);
    ASSERT_EQ(0x00U, this->m_emulator.registers[0x2D] & 0x01U);
}

void ManagerTester::testHighRateFailuresAreEdgeReported() {
    this->component.recordHighRateStatus(Operation::READ_SAMPLE, Status::SPI_ERROR);
    this->component.recordHighRateStatus(Operation::READ_SAMPLE, Status::SPI_ERROR);

    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::SPI_ERROR);
    ASSERT_EQ(2U, this->component.m_errors);

    this->component.recordHighRateStatus(Operation::READ_SAMPLE, Status::OK);
    this->component.recordHighRateStatus(Operation::READ_SAMPLE, Status::SPI_ERROR);
    ASSERT_EVENTS_OperationFailed_SIZE(2);
    ASSERT_EQ(3U, this->component.m_errors);
}

void ManagerTester::testReadBeforeInitializationIsRejected() {
    AccelerationSample sample;
    ASSERT_EQ(DriverStatus::INVALID_STATE, this->invoke_to_readAcceleration(0, sample));
    ASSERT_TRUE(this->m_emulator.transactions.empty());
    ASSERT_EVENTS_OperationFailed_SIZE(1);
    ASSERT_EVENTS_OperationFailed(0, Operation::READ_SAMPLE, DriverStatus::INVALID_STATE);
}

void ManagerTester::from_spi_handler(const NATIVE_INT_TYPE portNum,
                                     Fw::Buffer& writeBuffer,
                                     Fw::Buffer& readBuffer) {
    (void)portNum;
    ASSERT_EQ(writeBuffer.getSize(), readBuffer.getSize());
    ASSERT_TRUE(this->m_emulator.transfer(writeBuffer.getData(), readBuffer.getData(), writeBuffer.getSize()));
}

}  // namespace Adxl355
