#ifndef ADXL355_SYNCHRONIZED_APPLICATION_TESTER_HPP
#define ADXL355_SYNCHRONIZED_APPLICATION_TESTER_HPP

#include "Adxl355/Application/SynchronizedApplication.hpp"
#include "Adxl355/Application/SynchronizedApplicationGTestBase.hpp"
#include "Adxl355/Types/FppConstantsAc.hpp"

namespace Adxl355 {

class SynchronizedApplicationTester final : public SynchronizedApplicationGTestBase {
  public:
    static const U32 MAX_HISTORY_SIZE = 200;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    SynchronizedApplicationTester();
    ~SynchronizedApplicationTester();

    void testAcquisitionStartsDisabled();
    void testSevenSensorFrame();
    void testInitializeAllReportsPartialMask();
    void testInitializeAllInternalMode();
    void testInvalidSensorCountsAreRejected();
    void testPartialFrameIsEdgeReportedAndRecovers();
    void testDisabledAcquisitionDoesNoWork();
    void testReconfigurationIsRejectedWhileEnabled();
    void testIntermittentSensorEventsAreThrottled();
    void testOverlappingTriggerIsSkippedCountedAndLeavesASequenceGap();
    void testCommandsDuringInitializationReportBusy();
    void testEnablingRestartsEdgeDetection();
    void testFrameEdgesBeyondTheQueueAreDropped();
    void testSchedInPublishesWhileAFrameOwnsTheGate();

  private:
    enum class Reentry { NONE, TRIGGER, ENABLE_AND_COUNT, SCHED_IN };

    void connectPorts();
    void initComponents();

    DriverStatus from_configureSensor_handler(const FwIndexType portNum,
                                              const DeviceConfiguration& configuration);
    DriverStatus from_readSensor_handler(const FwIndexType portNum, AccelerationSample& sample);
    void from_frameReady_handler(const FwIndexType portNum, SampleFrame& frame);

    DriverStatus m_configureStatus[MAX_SENSORS];
    U32 m_configureCalls[MAX_SENSORS];
    DriverStatus m_sensorStatus[MAX_SENSORS];
    U32 m_sensorCalls[MAX_SENSORS];
    SampleFrame m_lastFrame;
    DeviceConfiguration m_lastConfiguration;
    U32 m_frameCalls;
    Reentry m_reentry;
    SynchronizedApplication component;
};

}  // namespace Adxl355

#endif
