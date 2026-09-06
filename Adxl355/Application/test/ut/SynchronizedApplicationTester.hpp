#ifndef ADXL355_SYNCHRONIZED_APPLICATION_TESTER_HPP
#define ADXL355_SYNCHRONIZED_APPLICATION_TESTER_HPP

#include "Adxl355/Application/SynchronizedApplication.hpp"
#include "Adxl355/Application/SynchronizedApplicationGTestBase.hpp"
#include "Adxl355/Types/FppConstantsAc.hpp"

namespace Adxl355 {

class SynchronizedApplicationTester final : public SynchronizedApplicationGTestBase {
  public:
    static const NATIVE_INT_TYPE MAX_HISTORY_SIZE = 100;
    static const NATIVE_INT_TYPE TEST_INSTANCE_ID = 0;

    SynchronizedApplicationTester();
    ~SynchronizedApplicationTester();

    void testAcquisitionStartsDisabled();
    void testSevenSensorFrame();
    void testInitializeAllReportsPartialMask();
    void testInitializeAllInternalMode();
    void testInvalidSensorCountsAreRejected();
    void testPartialFrameIsEdgeReportedAndRecovers();
    void testDisabledAcquisitionDoesNoWork();

  private:
    void connectPorts();
    void initComponents();

    DriverStatus from_configureSensor_handler(const NATIVE_INT_TYPE portNum, DeviceConfiguration& configuration);
    DriverStatus from_readSensor_handler(const NATIVE_INT_TYPE portNum, AccelerationSample& sample);
    void from_frameReady_handler(const NATIVE_INT_TYPE portNum, SampleFrame& frame);

    DriverStatus m_configureStatus[MAX_SENSORS];
    U32 m_configureCalls[MAX_SENSORS];
    DriverStatus m_sensorStatus[MAX_SENSORS];
    U32 m_sensorCalls[MAX_SENSORS];
    SampleFrame m_lastFrame;
    DeviceConfiguration m_lastConfiguration;
    U32 m_frameCalls;
    SynchronizedApplication component;
};

}  // namespace Adxl355

#endif
