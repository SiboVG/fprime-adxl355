#include "Adxl355/Application/SynchronizedApplication.hpp"
#include "Adxl355/Types/FppConstantsAc.hpp"

#include "Fw/Types/Assert.hpp"

namespace Adxl355 {

// Counters shared with REPORT must not hide a lock on the acquisition path.
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "acquisition flags require lock-free atomics");
static_assert(ATOMIC_CHAR_LOCK_FREE == 2, "acquisition masks require lock-free atomics");
static_assert((ATOMIC_LLONG_LOCK_FREE == 2) && (ATOMIC_LONG_LOCK_FREE == 2),
              "64-bit acquisition counters require lock-free atomics");

namespace {

U8 expectedMask(const U8 sensorCount) {
    return static_cast<U8>((1U << sensorCount) - 1U);
}

}  // namespace

SynchronizedApplication::SynchronizedApplication(const char* const componentName)
    : SynchronizedApplicationComponentBase(componentName),
      m_delay(),
      m_gate(),
      m_enabled(false),
      m_sensorCount(MAX_SENSORS),
      m_initializedMask(0U),
      m_lastValidMask(expectedMask(MAX_SENSORS)),
      m_sequence(0U),
      m_frames(0U),
      m_partialFrames(0U),
      m_skippedTriggers(0U),
      m_frameReports() {}

SynchronizedApplication::~SynchronizedApplication() {}

DriverStatus SynchronizedApplication::configure(const U8 sensorCount) {
    if ((sensorCount == 0U) || (sensorCount > MAX_SENSORS)) {
        return DriverStatus::INVALID_ARGUMENT;
    }
    if (this->m_enabled.load()) {
        return DriverStatus::INVALID_STATE;
    }
    GateHold hold(this->m_gate, this->m_gate.acquire(this->m_delay, CONTROL_ACQUIRE_ATTEMPTS));
    if (!hold.held()) {
        return DriverStatus::BUSY;
    }
    if (this->m_enabled.load()) {
        return DriverStatus::INVALID_STATE;
    }
    const U8 mask = expectedMask(sensorCount);
    this->m_sensorCount.store(sensorCount);
    this->m_lastValidMask.store(mask);
    this->m_initializedMask.store(static_cast<U8>(this->m_initializedMask.load() & mask));
    return DriverStatus::OK;
}

void SynchronizedApplication::INITIALIZE_ALL_cmdHandler(FwOpcodeType opCode,
                                                         U32 commandSequence,
                                                         const SensorRange& range,
                                                         const OutputDataRate& outputDataRate,
                                                         const HighPassCorner& highPassCorner) {
    const DeviceConfiguration configuration(range,
                                            outputDataRate,
                                            highPassCorner,
                                            SynchronizationMode::EXTERNAL_NO_INTERPOLATION,
                                            true,
                                            true,
                                            true,
                                            false,
                                            0U,
                                            96U);
    this->initializeAll(opCode, commandSequence, configuration);
}

void SynchronizedApplication::acquireFrame_handler(FwIndexType portNum, const U64 syncTimeMicroseconds) {
    (void)portNum;
    if (!this->m_enabled.load()) {
        return;
    }
    // Every enabled trigger consumes a sequence number, so a skipped trigger is
    // visible downstream as a sequence gap as well as in SkippedTriggers.
    const U64 sequence = this->m_sequence.fetch_add(1U);
    GateHold hold(this->m_gate, this->m_gate.tryAcquire());
    if (!hold.held()) {
        this->m_skippedTriggers.fetch_add(1U);
        return;
    }
    if (!this->m_enabled.load()) {
        return;  // disabled while this trigger was starting
    }

    const U8 sensorCount = this->m_sensorCount.load();
    FW_ASSERT((sensorCount >= 1U) && (sensorCount <= MAX_SENSORS), sensorCount);
    AccelerationSampleArray samples;
    DriverStatusArray statuses(DriverStatus::INVALID_STATE);
    U8 validMask = 0U;

    for (U8 sensor = 0U; sensor < sensorCount; ++sensor) {
        DriverStatus status = DriverStatus::INVALID_STATE;
        if (this->isConnected_readSensor_OutputPort(sensor)) {
            status = this->readSensor_out(sensor, samples[sensor]);
        }
        statuses[sensor] = status;
        if (status == DriverStatus::OK) {
            validMask = static_cast<U8>(validMask | static_cast<U8>(1U << sensor));
        }
    }

    const U8 completeMask = expectedMask(sensorCount);
    const U8 lastValidMask = this->m_lastValidMask.load();
    const bool partial = validMask != completeMask;
    // Edges only, and queued for schedIn: an event costs this path several
    // sensor reads.
    if (partial) {
        this->m_partialFrames.fetch_add(1U);
        if (validMask != lastValidMask) {
            (void)this->m_frameReports.push(FrameReport{sequence, validMask, false});
        }
    } else if (lastValidMask != completeMask) {
        (void)this->m_frameReports.push(FrameReport{sequence, validMask, true});
    }

    this->m_lastValidMask.store(validMask);
    this->m_frames.fetch_add(1U);

    SampleFrame frame(sequence, syncTimeMicroseconds, validMask, sensorCount, samples, statuses);
    if (this->isConnected_frameReady_OutputPort(0)) {
        this->frameReady_out(0, frame);
    }
}

void SynchronizedApplication::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    // No gate: the queue is lock-free and F Prime keeps event throttle counters
    // in atomics. At most one queue's worth of edges per call.
    FrameReport report = {0U, 0U, false};
    for (std::size_t edge = 0U; (edge < FRAME_REPORT_CAPACITY) && this->m_frameReports.pop(report); ++edge) {
        if (report.recovered) {
            this->log_ACTIVITY_HI_FrameRecovered(report.sequence);
        } else {
            this->log_WARNING_HI_FrameIncomplete(report.sequence, report.validMask);
        }
    }
    (void)this->m_frameReports.takeDropped();
}

void SynchronizedApplication::SET_SENSOR_COUNT_cmdHandler(FwOpcodeType opCode,
                                                           U32 commandSequence,
                                                           U8 sensorCount) {
    const DriverStatus status = this->configure(sensorCount);
    if (status == DriverStatus::INVALID_ARGUMENT) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (status == DriverStatus::INVALID_STATE) {
        this->rejectWhileEnabled(opCode, commandSequence);
        return;
    }
    if (status == DriverStatus::BUSY) {
        this->rejectBusy(opCode, commandSequence);
        return;
    }
    this->log_ACTIVITY_HI_SensorCountChanged(sensorCount);
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::SET_ENABLED_cmdHandler(FwOpcodeType opCode,
                                                      U32 commandSequence,
                                                      bool enabled) {
    if (enabled) {
        if (!this->m_enabled.load()) {
            GateHold hold(this->m_gate, this->m_gate.acquire(this->m_delay, CONTROL_ACQUIRE_ATTEMPTS));
            if (!hold.held()) {
                this->rejectBusy(opCode, commandSequence);
                return;
            }
            // Restart edge detection and event throttles for the new episode.
            this->m_lastValidMask.store(expectedMask(this->m_sensorCount.load()));
            this->clearEventThrottles();
            this->m_enabled.store(true);
        }
    } else {
        this->m_enabled.store(false);
        // Wait out a frame already in progress, so no acquisition work follows
        // the OK response.
        GateHold hold(this->m_gate, this->m_gate.acquire(this->m_delay, CONTROL_ACQUIRE_ATTEMPTS));
        if (!hold.held()) {
            this->rejectBusy(opCode, commandSequence);
            return;
        }
    }
    this->log_ACTIVITY_LO_AcquisitionStateChanged(enabled);
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::REPORT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    // Atomics only: REPORT never takes the gate, so it cannot cost a frame.
    this->reportTelemetry();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::INITIALIZE_ALL_INTERNAL_cmdHandler(FwOpcodeType opCode,
                                                                  U32 commandSequence,
                                                                  const SensorRange& range,
                                                                  const OutputDataRate& outputDataRate,
                                                                  const HighPassCorner& highPassCorner) {
    const DeviceConfiguration configuration(range,
                                            outputDataRate,
                                            highPassCorner,
                                            SynchronizationMode::INTERNAL,
                                            false,
                                            true,
                                            true,
                                            false,
                                            0U,
                                            96U);
    this->initializeAll(opCode, commandSequence, configuration);
}

void SynchronizedApplication::CLEAR_EVENT_THROTTLE_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    // The frame events are emitted by schedIn and their throttle counters are
    // atomic, so no gate is needed and the command never costs a frame.
    this->clearEventThrottles();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::initializeAll(FwOpcodeType opCode,
                                             U32 commandSequence,
                                             const DeviceConfiguration& configuration) {
    if (this->m_enabled.load()) {
        this->rejectWhileEnabled(opCode, commandSequence);
        return;
    }
    // Blocks the command thread for up to the sum of the managers' worst-case
    // initialization times (see the architecture document).
    GateHold hold(this->m_gate, this->m_gate.acquire(this->m_delay, CONTROL_ACQUIRE_ATTEMPTS));
    if (!hold.held()) {
        this->rejectBusy(opCode, commandSequence);
        return;
    }
    if (this->m_enabled.load()) {
        this->rejectWhileEnabled(opCode, commandSequence);
        return;
    }
    const U8 sensorCount = this->m_sensorCount.load();
    U8 initializedMask = 0U;
    for (U8 sensor = 0U; sensor < sensorCount; ++sensor) {
        DriverStatus status = DriverStatus::INVALID_STATE;
        if (this->isConnected_configureSensor_OutputPort(sensor)) {
            status = this->configureSensor_out(sensor, configuration);
        }
        if (status == DriverStatus::OK) {
            initializedMask = static_cast<U8>(initializedMask | static_cast<U8>(1U << sensor));
        }
    }

    this->m_initializedMask.store(initializedMask);
    const U8 completeMask = expectedMask(sensorCount);
    if (initializedMask == completeMask) {
        this->log_ACTIVITY_HI_ArrayInitializationComplete(initializedMask);
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
    } else {
        this->log_WARNING_HI_ArrayInitializationIncomplete(initializedMask, completeMask);
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
    }
    this->tlmWrite_InitializedMask(initializedMask);
}

void SynchronizedApplication::rejectBusy(FwOpcodeType opCode, U32 commandSequence) {
    this->log_WARNING_LO_ApplicationBusy();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
}

void SynchronizedApplication::rejectWhileEnabled(FwOpcodeType opCode, U32 commandSequence) {
    this->log_WARNING_LO_ReconfigurationRejected();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
}

void SynchronizedApplication::clearEventThrottles() {
    this->log_WARNING_HI_FrameIncomplete_ThrottleClear();
    this->log_ACTIVITY_HI_FrameRecovered_ThrottleClear();
}

void SynchronizedApplication::reportTelemetry() {
    this->tlmWrite_Frames(this->m_frames.load());
    this->tlmWrite_PartialFrames(this->m_partialFrames.load());
    this->tlmWrite_LastValidMask(this->m_lastValidMask.load());
    this->tlmWrite_SensorCount(this->m_sensorCount.load());
    this->tlmWrite_Enabled(this->m_enabled.load() ? 1U : 0U);
    this->tlmWrite_InitializedMask(this->m_initializedMask.load());
    this->tlmWrite_SkippedTriggers(this->m_skippedTriggers.load());
}

}  // namespace Adxl355
