#include "SynchronizedApplicationTester.hpp"

TEST(Nominal, SevenSensorFrame) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testSevenSensorFrame();
}

TEST(OffNominal, InitializeAllReportsPartialMask) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testInitializeAllReportsPartialMask();
}

TEST(Nominal, InitializeAllInternalMode) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testInitializeAllInternalMode();
}

TEST(Nominal, AcquisitionStartsDisabled) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testAcquisitionStartsDisabled();
}

TEST(OffNominal, InvalidSensorCountsAreRejected) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testInvalidSensorCountsAreRejected();
}

TEST(OffNominal, PartialFrameIsEdgeReportedAndRecovers) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testPartialFrameIsEdgeReportedAndRecovers();
}

TEST(Nominal, DisabledAcquisitionDoesNoWork) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testDisabledAcquisitionDoesNoWork();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
