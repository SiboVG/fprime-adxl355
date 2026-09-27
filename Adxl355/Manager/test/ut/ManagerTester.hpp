#ifndef ADXL355_MANAGER_TESTER_HPP
#define ADXL355_MANAGER_TESTER_HPP

#include "Adxl355/Manager/Manager.hpp"
#include "Adxl355/Manager/ManagerGTestBase.hpp"
#include "tests/TestSupport.hpp"

namespace Adxl355 {

class ManagerTester final : public ManagerGTestBase {
  public:
    static const U32 MAX_HISTORY_SIZE = 200;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    ManagerTester();
    ~ManagerTester();

    void testConfigureAndRead();
    void testControlCommands();
    void testCommandValidationErrorsAreNotDeviceFaults();
    void testHighRateFailuresAreEdgeReportedAndThrottled();
    void testReadBeforeInitializationIsRejected();
    void testImplausibleBusDataIsRejected();
    void testHighRateReadNeverWaitsForAControlOperation();
    void testFailedInitializationReportsConsistentLifecycle();
    void testTemperatureAndMeasurementControlPorts();
    void testSnapshotDoesNotConsumeDataReady();
    void testNvmMismatchIsReported();
    void testFailedSelfTestInvalidatesTheDevice();
    void testEveryControlPathReportsBusyWithoutBlocking();
    void testDriverFailuresPropagateThroughTheSpiPort();

  private:
    enum class Reentry { NONE, READ_ACCELERATION, STOP_COMMAND, EVERY_CONTROL };

    void connectPorts();
    void initComponents();
    void initialize();

    Drv::SpiStatus from_spi_handler(const FwIndexType portNum, Fw::Buffer& writeBuffer, Fw::Buffer& readBuffer);

    Adxl355Test::RegisterSpi m_emulator;
    Reentry m_reentry;
    DriverStatus m_reentrantStatus;
    DriverStatus m_reentrantPortStatus[3];
    Drv::SpiStatus m_driverStatus;
    bool m_shortenReadBuffer;
    Manager component;
};

}  // namespace Adxl355

#endif
