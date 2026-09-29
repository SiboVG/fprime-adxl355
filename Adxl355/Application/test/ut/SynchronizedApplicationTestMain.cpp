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

TEST(OffNominal, ReconfigurationIsRejectedWhileEnabled) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testReconfigurationIsRejectedWhileEnabled();
}

TEST(OffNominal, IntermittentSensorEventsAreThrottled) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testIntermittentSensorEventsAreThrottled();
}

TEST(Concurrency, OverlappingTriggerIsSkippedCountedAndLeavesASequenceGap) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testOverlappingTriggerIsSkippedCountedAndLeavesASequenceGap();
}

TEST(Concurrency, CommandsDuringInitializationReportBusy) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testCommandsDuringInitializationReportBusy();
}

TEST(Nominal, EnablingRestartsEdgeDetection) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testEnablingRestartsEdgeDetection();
}

TEST(OffNominal, FrameEdgesBeyondTheQueueAreDropped) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testFrameEdgesBeyondTheQueueAreDropped();
}

TEST(Concurrency, SchedInPublishesWhileAFrameOwnsTheGate) {
    Adxl355::SynchronizedApplicationTester tester;
    tester.testSchedInPublishesWhileAFrameOwnsTheGate();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
