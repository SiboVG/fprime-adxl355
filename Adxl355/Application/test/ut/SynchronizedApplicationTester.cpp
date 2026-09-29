#include "SynchronizedApplicationTester.hpp"

namespace Adxl355 {

SynchronizedApplicationTester::SynchronizedApplicationTester()
    : SynchronizedApplicationGTestBase("SynchronizedApplicationTester", MAX_HISTORY_SIZE),
      m_configureStatus(),
      m_configureCalls(),
      m_sensorStatus(),
      m_sensorCalls(),
      m_lastFrame(),
      m_lastConfiguration(),
      m_frameCalls(0U),
      m_reentry(Reentry::NONE),
      component("SynchronizedApplication") {
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        this->m_configureStatus[index] = DriverStatus::OK;
        this->m_configureCalls[index] = 0U;
        this->m_sensorStatus[index] = DriverStatus::OK;
        this->m_sensorCalls[index] = 0U;
    }
    this->initComponents();
    this->connectPorts();
}

SynchronizedApplicationTester::~SynchronizedApplicationTester() {}

void SynchronizedApplicationTester::testInitializeAllReportsPartialMask() {
    this->m_configureStatus[4] = DriverStatus::SPI_ERROR;
    this->sendCmd_INITIALIZE_ALL(0,
                                 10U,
                                 SensorRange::G2,
                                 OutputDataRate::HZ_4000,
                                 HighPassCorner::OFF);

    ASSERT_CMD_RESPONSE(0, SynchronizedApplication::OPCODE_INITIALIZE_ALL, 10U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ArrayInitializationIncomplete_SIZE(1);
    ASSERT_EVENTS_ArrayInitializationIncomplete(0, 0x6FU, 0x7FU);
    ASSERT_TLM_InitializedMask(0, 0x6FU);
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(1U, this->m_configureCalls[index]);
    }
    ASSERT_EQ(SynchronizationMode::EXTERNAL_NO_INTERPOLATION,
              this->m_lastConfiguration.get_synchronizationMode());
    ASSERT_TRUE(this->m_lastConfiguration.get_externalClock());
    ASSERT_EQ(OutputDataRate::HZ_4000, this->m_lastConfiguration.get_outputDataRate());
    ASSERT_EQ(0U, this->m_lastConfiguration.get_interruptMap());
}

void SynchronizedApplicationTester::testInitializeAllInternalMode() {
    ASSERT_EQ(DriverStatus::OK, this->component.configure(2U));
    this->sendCmd_INITIALIZE_ALL_INTERNAL(0,
                                         11U,
                                         SensorRange::G4,
                                         OutputDataRate::HZ_125,
                                         HighPassCorner::OFF);

    ASSERT_CMD_RESPONSE(0,
                        SynchronizedApplication::OPCODE_INITIALIZE_ALL_INTERNAL,
                        11U,
                        Fw::CmdResponse::OK);
    ASSERT_EVENTS_ArrayInitializationComplete_SIZE(1);
    ASSERT_EVENTS_ArrayInitializationComplete(0, 0x03U);
    ASSERT_TLM_InitializedMask(0, 0x03U);
    ASSERT_EQ(1U, this->m_configureCalls[0]);
    ASSERT_EQ(1U, this->m_configureCalls[1]);
    for (U32 index = 2U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(0U, this->m_configureCalls[index]);
    }
    ASSERT_EQ(SynchronizationMode::INTERNAL, this->m_lastConfiguration.get_synchronizationMode());
    ASSERT_FALSE(this->m_lastConfiguration.get_externalClock());
    ASSERT_EQ(SensorRange::G4, this->m_lastConfiguration.get_range());
    ASSERT_EQ(OutputDataRate::HZ_125, this->m_lastConfiguration.get_outputDataRate());
}

void SynchronizedApplicationTester::testInvalidSensorCountsAreRejected() {
    this->sendCmd_SET_SENSOR_COUNT(0, 20U, 0U);
    ASSERT_CMD_RESPONSE(0,
                        SynchronizedApplication::OPCODE_SET_SENSOR_COUNT,
                        20U,
                        Fw::CmdResponse::VALIDATION_ERROR);

    this->sendCmd_SET_SENSOR_COUNT(0, 21U, static_cast<U8>(MAX_SENSORS + 1U));
    ASSERT_CMD_RESPONSE(1,
                        SynchronizedApplication::OPCODE_SET_SENSOR_COUNT,
                        21U,
                        Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EQ(DriverStatus::INVALID_ARGUMENT, this->component.configure(0U));

    this->sendCmd_REPORT(0, 22U);
    ASSERT_TLM_SensorCount(0, MAX_SENSORS);

    this->sendCmd_SET_SENSOR_COUNT(0, 23U, 3U);
    ASSERT_CMD_RESPONSE(3, SynchronizedApplication::OPCODE_SET_SENSOR_COUNT, 23U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_SensorCountChanged(0, 3U);
}

void SynchronizedApplicationTester::testAcquisitionStartsDisabled() {
    this->invoke_to_acquireFrame(0, 500U);

    ASSERT_EQ(0U, this->m_frameCalls);
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(0U, this->m_sensorCalls[index]);
    }
}

void SynchronizedApplicationTester::testSevenSensorFrame() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    ASSERT_CMD_RESPONSE(0, SynchronizedApplication::OPCODE_SET_ENABLED, 1U, Fw::CmdResponse::OK);
    this->invoke_to_acquireFrame(0, 1000000U);

    ASSERT_EQ(1U, this->m_frameCalls);
    ASSERT_EQ(0U, this->m_lastFrame.get_sequence());
    ASSERT_EQ(1000000U, this->m_lastFrame.get_syncTimeMicroseconds());
    ASSERT_EQ(0x7FU, this->m_lastFrame.get_validMask());
    ASSERT_EQ(7U, this->m_lastFrame.get_sensorCount());
    ASSERT_EQ(1, this->m_lastFrame.get_samples()[0].get_rawX());
    ASSERT_EQ(7, this->m_lastFrame.get_samples()[6].get_rawX());
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(1U, this->m_sensorCalls[index]);
    }
    ASSERT_TLM_Frames_SIZE(0);
}

void SynchronizedApplicationTester::testPartialFrameIsEdgeReportedAndRecovers() {
    this->sendCmd_SET_ENABLED(0, 2U, true);
    ASSERT_CMD_RESPONSE(0, SynchronizedApplication::OPCODE_SET_ENABLED, 2U, Fw::CmdResponse::OK);
    this->m_sensorStatus[2] = DriverStatus::SPI_ERROR;
    this->invoke_to_acquireFrame(0, 10U);
    ASSERT_EQ(0x7BU, this->m_lastFrame.get_validMask());
    ASSERT_EQ(DriverStatus::SPI_ERROR, this->m_lastFrame.get_sensorStatus()[2]);
    // The frame itself emits nothing; schedIn publishes the edge.
    ASSERT_EVENTS_FrameIncomplete_SIZE(0);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(1);
    ASSERT_EVENTS_FrameIncomplete(0, 0U, 0x7BU);

    this->invoke_to_acquireFrame(0, 20U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(1);

    this->m_sensorStatus[2] = DriverStatus::OK;
    this->invoke_to_acquireFrame(0, 30U);
    ASSERT_EVENTS_FrameRecovered_SIZE(0);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameRecovered_SIZE(1);
    ASSERT_EVENTS_FrameRecovered(0, 2U);

    this->sendCmd_REPORT(0, 1U);
    ASSERT_CMD_RESPONSE(1, SynchronizedApplication::OPCODE_REPORT, 1U, Fw::CmdResponse::OK);
    ASSERT_TLM_Frames(0, 3U);
    ASSERT_TLM_PartialFrames(0, 2U);
    ASSERT_TLM_LastValidMask(0, 0x7FU);
    ASSERT_TLM_SkippedTriggers(0, 0U);
    ASSERT_TLM_Enabled(0, 1U);
}

void SynchronizedApplicationTester::testDisabledAcquisitionDoesNoWork() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    this->sendCmd_SET_ENABLED(0, 2U, false);
    ASSERT_CMD_RESPONSE(1, SynchronizedApplication::OPCODE_SET_ENABLED, 2U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_AcquisitionStateChanged(1, false);
    this->invoke_to_acquireFrame(0, 42U);
    ASSERT_EQ(0U, this->m_frameCalls);
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(0U, this->m_sensorCalls[index]);
    }
}

void SynchronizedApplicationTester::testReconfigurationIsRejectedWhileEnabled() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    this->sendCmd_INITIALIZE_ALL(0, 2U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
    this->sendCmd_INITIALIZE_ALL_INTERNAL(0, 3U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
    this->sendCmd_SET_SENSOR_COUNT(0, 4U, 3U);
    ASSERT_CMD_RESPONSE(1, SynchronizedApplication::OPCODE_INITIALIZE_ALL, 2U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(2,
                        SynchronizedApplication::OPCODE_INITIALIZE_ALL_INTERNAL,
                        3U,
                        Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(3, SynchronizedApplication::OPCODE_SET_SENSOR_COUNT, 4U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ReconfigurationRejected_SIZE(3);
    ASSERT_EQ(DriverStatus::INVALID_STATE, this->component.configure(3U));
    for (U32 index = 0U; index < MAX_SENSORS; ++index) {
        ASSERT_EQ(0U, this->m_configureCalls[index]);
    }

    this->sendCmd_SET_ENABLED(0, 5U, false);
    this->sendCmd_SET_SENSOR_COUNT(0, 6U, 3U);
    ASSERT_CMD_RESPONSE(5, SynchronizedApplication::OPCODE_SET_SENSOR_COUNT, 6U, Fw::CmdResponse::OK);
}

void SynchronizedApplicationTester::testIntermittentSensorEventsAreThrottled() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    for (U32 frame = 0U; frame < 50U; ++frame) {
        this->m_sensorStatus[2] = ((frame % 2U) == 0U) ? DriverStatus::SPI_ERROR : DriverStatus::OK;
        this->invoke_to_acquireFrame(0, frame);
        if ((frame % 10U) == 9U) {
            this->invoke_to_schedIn(0, 0U);  // ten edges per call fit the queue
        }
    }
    ASSERT_EQ(50U, this->m_frameCalls);
    ASSERT_EVENTS_FrameIncomplete_SIZE(10);
    ASSERT_EVENTS_FrameRecovered_SIZE(10);

    // The clear takes effect at once, also while acquisition runs, and needs
    // no gate; afterwards the events flow again.
    this->sendCmd_CLEAR_EVENT_THROTTLE(0, 2U);
    ASSERT_CMD_RESPONSE(1, SynchronizedApplication::OPCODE_CLEAR_EVENT_THROTTLE, 2U, Fw::CmdResponse::OK);
    this->m_sensorStatus[2] = DriverStatus::SPI_ERROR;
    this->invoke_to_acquireFrame(0, 100U);
    this->m_sensorStatus[2] = DriverStatus::OK;
    this->invoke_to_acquireFrame(0, 101U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(11);
    ASSERT_EVENTS_FrameRecovered_SIZE(11);

    this->sendCmd_REPORT(0, 3U);
    ASSERT_TLM_PartialFrames(0, 26U);
    ASSERT_TLM_Frames(0, 52U);

    this->sendCmd_SET_ENABLED(0, 4U, false);
    this->sendCmd_CLEAR_EVENT_THROTTLE(0, 5U);
    ASSERT_CMD_RESPONSE(4, SynchronizedApplication::OPCODE_CLEAR_EVENT_THROTTLE, 5U, Fw::CmdResponse::OK);
}

void SynchronizedApplicationTester::testOverlappingTriggerIsSkippedCountedAndLeavesASequenceGap() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    this->m_reentry = Reentry::TRIGGER;
    this->invoke_to_acquireFrame(0, 10U);
    ASSERT_EQ(1U, this->m_frameCalls);
    ASSERT_EQ(0U, this->m_lastFrame.get_sequence());

    this->invoke_to_acquireFrame(0, 20U);
    ASSERT_EQ(2U, this->m_lastFrame.get_sequence());

    this->sendCmd_REPORT(0, 2U);
    ASSERT_TLM_SkippedTriggers(0, 1U);
    ASSERT_TLM_Frames(0, 2U);
}

void SynchronizedApplicationTester::testCommandsDuringInitializationReportBusy() {
    // While INITIALIZE_ALL owns the gate, other commands wait a bounded time
    // and report instead of blocking indefinitely.
    this->m_reentry = Reentry::ENABLE_AND_COUNT;
    this->sendCmd_INITIALIZE_ALL(0, 1U, SensorRange::G2, OutputDataRate::HZ_4000, HighPassCorner::OFF);
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(0, SynchronizedApplication::OPCODE_SET_ENABLED, 7U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(1, SynchronizedApplication::OPCODE_SET_SENSOR_COUNT, 8U, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(2, SynchronizedApplication::OPCODE_INITIALIZE_ALL, 1U, Fw::CmdResponse::OK);
    ASSERT_EVENTS_ApplicationBusy_SIZE(2);

    this->sendCmd_REPORT(0, 2U);
    ASSERT_TLM_Enabled(0, 0U);
    ASSERT_TLM_SensorCount(0, MAX_SENSORS);
}

void SynchronizedApplicationTester::testEnablingRestartsEdgeDetection() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    this->m_sensorStatus[0] = DriverStatus::STALE_DATA;
    this->invoke_to_acquireFrame(0, 1U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(1);
    this->sendCmd_SET_ENABLED(0, 2U, false);
    this->sendCmd_SET_ENABLED(0, 3U, true);
    this->sendCmd_SET_ENABLED(0, 4U, true);
    this->invoke_to_acquireFrame(0, 2U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(2);
    ASSERT_CMD_RESPONSE(3, SynchronizedApplication::OPCODE_SET_ENABLED, 4U, Fw::CmdResponse::OK);
}

void SynchronizedApplicationTester::testFrameEdgesBeyondTheQueueAreDropped() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    // Twenty alternating frames without schedIn are twenty edges; the queue
    // keeps the first sixteen, eight of each kind, below the throttle of ten.
    for (U32 frame = 0U; frame < 20U; ++frame) {
        this->m_sensorStatus[4] = ((frame % 2U) == 0U) ? DriverStatus::STALE_DATA : DriverStatus::OK;
        this->invoke_to_acquireFrame(0, frame);
    }
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(8);
    ASSERT_EVENTS_FrameRecovered_SIZE(8);
    ASSERT_EVENTS_FrameIncomplete(0, 0U, 0x6FU);
    ASSERT_EVENTS_FrameRecovered(7, 15U);
    this->invoke_to_schedIn(0, 0U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(8);

    // Every partial frame is still counted.
    this->sendCmd_REPORT(0, 2U);
    ASSERT_TLM_PartialFrames(0, 10U);
    ASSERT_TLM_Frames(0, 20U);
}

void SynchronizedApplicationTester::testSchedInPublishesWhileAFrameOwnsTheGate() {
    this->sendCmd_SET_ENABLED(0, 1U, true);
    this->m_sensorStatus[1] = DriverStatus::SPI_ERROR;
    this->invoke_to_acquireFrame(0, 1U);
    // The next frame calls schedIn from its first sensor read, while it owns
    // the gate: the queued edge is published and the frame completes.
    this->m_reentry = Reentry::SCHED_IN;
    this->invoke_to_acquireFrame(0, 2U);
    ASSERT_EVENTS_FrameIncomplete_SIZE(1);
    ASSERT_EVENTS_FrameIncomplete(0, 0U, 0x7DU);
    ASSERT_EVENTS_ApplicationBusy_SIZE(0);
    ASSERT_EQ(2U, this->m_frameCalls);
}

DriverStatus SynchronizedApplicationTester::from_readSensor_handler(const FwIndexType portNum,
                                                                    AccelerationSample& sample) {
    if (this->m_reentry == Reentry::TRIGGER) {
        this->m_reentry = Reentry::NONE;
        this->invoke_to_acquireFrame(0, 11U);
    } else if (this->m_reentry == Reentry::SCHED_IN) {
        this->m_reentry = Reentry::NONE;
        this->invoke_to_schedIn(0, 0U);
    }
    ++this->m_sensorCalls[portNum];
    sample.set(static_cast<I32>(portNum + 1),
               -static_cast<I32>(portNum + 1),
               static_cast<I32>((portNum + 1) * 10),
               static_cast<F64>(portNum + 1),
               -static_cast<F64>(portNum + 1),
               static_cast<F64>((portNum + 1) * 10));
    return this->m_sensorStatus[portNum];
}

DriverStatus SynchronizedApplicationTester::from_configureSensor_handler(
    const FwIndexType portNum,
    const DeviceConfiguration& configuration) {
    if (this->m_reentry == Reentry::ENABLE_AND_COUNT) {
        this->m_reentry = Reentry::NONE;
        this->sendCmd_SET_ENABLED(0, 7U, true);
        this->sendCmd_SET_SENSOR_COUNT(0, 8U, 2U);
    }
    ++this->m_configureCalls[portNum];
    this->m_lastConfiguration = configuration;
    return this->m_configureStatus[portNum];
}

void SynchronizedApplicationTester::from_frameReady_handler(const FwIndexType portNum, SampleFrame& frame) {
    (void)portNum;
    this->m_lastFrame = frame;
    ++this->m_frameCalls;
    this->pushFromPortEntry_frameReady(frame);
}

}  // namespace Adxl355
