#include "Adxl355/Application/SynchronizedApplication.hpp"
#include "Adxl355/Types/FppConstantsAc.hpp"

namespace Adxl355 {

namespace {

U8 expectedMask(const U8 sensorCount) {
    return static_cast<U8>((1U << sensorCount) - 1U);
}

}  // namespace

SynchronizedApplication::SynchronizedApplication(const char* const componentName)
    : SynchronizedApplicationComponentBase(componentName),
      m_sensorCount(MAX_SENSORS),
      m_enabled(false),
      m_sequence(0U),
      m_frames(0U),
      m_partialFrames(0U),
      m_lastValidMask(expectedMask(MAX_SENSORS)),
      m_initializedMask(0U) {}

SynchronizedApplication::~SynchronizedApplication() {}

bool SynchronizedApplication::configure(const U8 sensorCount) {
    if ((sensorCount == 0U) || (sensorCount > MAX_SENSORS)) {
        return false;
    }
    this->m_sensorCount = sensorCount;
    this->m_lastValidMask = expectedMask(sensorCount);
    this->m_initializedMask = static_cast<U8>(this->m_initializedMask & this->m_lastValidMask);
    return true;
}

void SynchronizedApplication::INITIALIZE_ALL_cmdHandler(FwOpcodeType opCode,
                                                         U32 commandSequence,
                                                         SensorRange range,
                                                         OutputDataRate outputDataRate,
                                                         HighPassCorner highPassCorner) {
    DeviceConfiguration configuration(range,
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
    if (!this->m_enabled) {
        return;
    }

    AccelerationSampleArray samples;
    DriverStatusArray statuses(DriverStatus::INVALID_STATE);
    U8 validMask = 0U;

    for (U8 sensor = 0U; sensor < this->m_sensorCount; ++sensor) {
        DriverStatus status = DriverStatus::INVALID_STATE;
        if (this->isConnected_readSensor_OutputPort(sensor)) {
            status = this->readSensor_out(sensor, samples[sensor]);
        }
        statuses[sensor] = status;
        if (status == DriverStatus::OK) {
            validMask = static_cast<U8>(validMask | static_cast<U8>(1U << sensor));
        }
    }

    const U8 completeMask = expectedMask(this->m_sensorCount);
    const bool partial = validMask != completeMask;
    if (partial) {
        ++this->m_partialFrames;
        if (validMask != this->m_lastValidMask) {
            this->log_WARNING_HI_FrameIncomplete(this->m_sequence, validMask);
        }
    } else if (this->m_lastValidMask != completeMask) {
        this->log_ACTIVITY_HI_FrameRecovered(this->m_sequence);
    }

    this->m_lastValidMask = validMask;
    ++this->m_frames;

    SampleFrame frame(this->m_sequence,
                      syncTimeMicroseconds,
                      validMask,
                      this->m_sensorCount,
                      samples,
                      statuses);
    if (this->isConnected_frameReady_OutputPort(0)) {
        this->frameReady_out(0, frame);
    }
    ++this->m_sequence;
}

void SynchronizedApplication::SET_SENSOR_COUNT_cmdHandler(FwOpcodeType opCode,
                                                           U32 commandSequence,
                                                           U8 sensorCount) {
    if (!this->configure(sensorCount)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->log_ACTIVITY_HI_SensorCountChanged(sensorCount);
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::SET_ENABLED_cmdHandler(FwOpcodeType opCode,
                                                      U32 commandSequence,
                                                      bool enabled) {
    this->m_enabled = enabled;
    this->log_ACTIVITY_LO_AcquisitionStateChanged(enabled);
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::REPORT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    this->reportTelemetry();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

void SynchronizedApplication::INITIALIZE_ALL_INTERNAL_cmdHandler(FwOpcodeType opCode,
                                                                  U32 commandSequence,
                                                                  SensorRange range,
                                                                  OutputDataRate outputDataRate,
                                                                  HighPassCorner highPassCorner) {
    DeviceConfiguration configuration(range,
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

void SynchronizedApplication::initializeAll(FwOpcodeType opCode,
                                             U32 commandSequence,
                                             DeviceConfiguration& configuration) {
    U8 initializedMask = 0U;
    for (U8 sensor = 0U; sensor < this->m_sensorCount; ++sensor) {
        DriverStatus status = DriverStatus::INVALID_STATE;
        if (this->isConnected_configureSensor_OutputPort(sensor)) {
            status = this->configureSensor_out(sensor, configuration);
        }
        if (status == DriverStatus::OK) {
            initializedMask = static_cast<U8>(initializedMask | static_cast<U8>(1U << sensor));
        }
    }

    this->m_initializedMask = initializedMask;
    const U8 completeMask = expectedMask(this->m_sensorCount);
    if (initializedMask == completeMask) {
        this->log_ACTIVITY_HI_ArrayInitializationComplete(initializedMask);
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
    } else {
        this->log_WARNING_HI_ArrayInitializationIncomplete(initializedMask, completeMask);
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
    }
    this->tlmWrite_InitializedMask(initializedMask);
}

void SynchronizedApplication::reportTelemetry() {
    this->tlmWrite_Frames(this->m_frames);
    this->tlmWrite_PartialFrames(this->m_partialFrames);
    this->tlmWrite_LastValidMask(this->m_lastValidMask);
    this->tlmWrite_SensorCount(this->m_sensorCount);
    this->tlmWrite_Enabled(this->m_enabled ? 1U : 0U);
    this->tlmWrite_InitializedMask(this->m_initializedMask);
}

}  // namespace Adxl355
