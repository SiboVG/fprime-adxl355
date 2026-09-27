#include "ManagerTester.hpp"

TEST(Nominal, ConfigureAndRead) {
    Adxl355::ManagerTester tester;
    tester.testConfigureAndRead();
}

TEST(Nominal, ControlCommands) {
    Adxl355::ManagerTester tester;
    tester.testControlCommands();
}

TEST(Nominal, TemperatureAndMeasurementControlPorts) {
    Adxl355::ManagerTester tester;
    tester.testTemperatureAndMeasurementControlPorts();
}

TEST(Nominal, SnapshotDoesNotConsumeDataReady) {
    Adxl355::ManagerTester tester;
    tester.testSnapshotDoesNotConsumeDataReady();
}

TEST(OffNominal, CommandValidationErrorsAreNotDeviceFaults) {
    Adxl355::ManagerTester tester;
    tester.testCommandValidationErrorsAreNotDeviceFaults();
}

TEST(OffNominal, HighRateFailuresAreEdgeReportedAndThrottled) {
    Adxl355::ManagerTester tester;
    tester.testHighRateFailuresAreEdgeReportedAndThrottled();
}

TEST(OffNominal, ReadBeforeInitializationIsRejected) {
    Adxl355::ManagerTester tester;
    tester.testReadBeforeInitializationIsRejected();
}

TEST(OffNominal, ImplausibleBusDataIsRejected) {
    Adxl355::ManagerTester tester;
    tester.testImplausibleBusDataIsRejected();
}

TEST(Concurrency, HighRateReadNeverWaitsForAControlOperation) {
    Adxl355::ManagerTester tester;
    tester.testHighRateReadNeverWaitsForAControlOperation();
}

TEST(OffNominal, FailedInitializationReportsConsistentLifecycle) {
    Adxl355::ManagerTester tester;
    tester.testFailedInitializationReportsConsistentLifecycle();
}

TEST(OffNominal, NvmMismatchIsReported) {
    Adxl355::ManagerTester tester;
    tester.testNvmMismatchIsReported();
}

TEST(OffNominal, FailedSelfTestInvalidatesTheDevice) {
    Adxl355::ManagerTester tester;
    tester.testFailedSelfTestInvalidatesTheDevice();
}

TEST(Concurrency, EveryControlPathReportsBusyWithoutBlocking) {
    Adxl355::ManagerTester tester;
    tester.testEveryControlPathReportsBusyWithoutBlocking();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
