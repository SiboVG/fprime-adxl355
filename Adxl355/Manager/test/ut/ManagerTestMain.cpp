#include "ManagerTester.hpp"

TEST(Nominal, ConfigureAndRead) {
    Adxl355::ManagerTester tester;
    tester.testConfigureAndRead();
}

TEST(Nominal, ControlCommands) {
    Adxl355::ManagerTester tester;
    tester.testControlCommands();
}

TEST(OffNominal, HighRateFailuresAreEdgeReported) {
    Adxl355::ManagerTester tester;
    tester.testHighRateFailuresAreEdgeReported();
}

TEST(OffNominal, ReadBeforeInitializationIsRejected) {
    Adxl355::ManagerTester tester;
    tester.testReadBeforeInitializationIsRejected();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
