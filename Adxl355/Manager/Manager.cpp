#include "Adxl355/Manager/Manager.hpp"

#include "Fw/Buffer/Buffer.hpp"
#include "Fw/Types/Assert.hpp"

namespace Adxl355 {

// schedIn reads these without the gate; a hidden lock would block the 4 kHz path.
static_assert(ATOMIC_CHAR_LOCK_FREE == 2, "the published status requires a lock-free atomic");
static_assert((ATOMIC_LLONG_LOCK_FREE == 2) && (ATOMIC_LONG_LOCK_FREE == 2),
              "the error counter requires lock-free 64-bit atomics");

Manager::PortSpiBus::PortSpiBus(Manager& owner) : m_owner(owner) {}

bool Manager::PortSpiBus::transfer(const std::uint8_t* transmit,
                                   std::uint8_t* receive,
                                   std::size_t size) {
    if ((transmit == nullptr) || (receive == nullptr) || (size == 0U) ||
        !this->m_owner.isConnected_spi_OutputPort(0)) {
        return false;
    }
    Fw::Buffer writeBuffer(const_cast<U8*>(reinterpret_cast<const U8*>(transmit)), static_cast<FwSizeType>(size));
    Fw::Buffer readBuffer(reinterpret_cast<U8*>(receive), static_cast<FwSizeType>(size));
    // A transfer counts only if the driver reports success and leaves both
    // buffers as they were handed over.
    const Drv::SpiStatus status = this->m_owner.spi_out(0, writeBuffer, readBuffer);
    return (status == Drv::SpiStatus::SPI_OK) && (writeBuffer.getSize() == size) && (readBuffer.getSize() == size) &&
           (writeBuffer.getData() == transmit) && (readBuffer.getData() == receive);
}

Manager::Manager(const char* const componentName)
    : ManagerComponentBase(componentName),
      m_spiBus(*this),
      m_delay(),
      m_device(this->m_spiBus, this->m_delay),
      m_gate(),
      m_samplesRead(0U),
      m_errors(0U),
      m_lastHighRateStatus(Status::OK),
      m_publishedHighRateStatus(Status::OK),
      m_highRateReports() {}

Manager::~Manager() {}

DriverStatus Manager::configure_handler(FwIndexType portNum, const DeviceConfiguration& configuration) {
    (void)portNum;
    if (!this->acquireForControl(Operation::INITIALIZE)) {
        return DriverStatus::BUSY;
    }
    GateHold hold(this->m_gate, true);
    return Manager::toFppStatus(this->initializeDevice(Manager::toCoreConfiguration(configuration)));
}

DriverStatus Manager::readAcceleration_handler(FwIndexType portNum, AccelerationSample& sample) {
    (void)portNum;
    // The 4 kHz path never waits: a control operation in progress costs this
    // sensor one frame instead of stalling every sensor behind it. Nothing is
    // recorded without the gate, because the counters belong to its owner.
    GateHold hold(this->m_gate, this->m_gate.tryAcquire());
    if (!hold.held()) {
        return DriverStatus::BUSY;
    }
    if (!this->m_device.isMeasuring()) {
        this->recordHighRateStatus(Status::INVALID_STATE);
        return DriverStatus::INVALID_STATE;
    }
    Sample coreSample;
    const Status status = this->m_device.readSample(coreSample);
    if (status == Status::OK) {
        sample.set(coreSample.raw.x,
                   coreSample.raw.y,
                   coreSample.raw.z,
                   coreSample.xMetersPerSecondSquared,
                   coreSample.yMetersPerSecondSquared,
                   coreSample.zMetersPerSecondSquared);
        ++this->m_samplesRead;
    }
    this->recordHighRateStatus(status);
    return Manager::toFppStatus(status);
}

DriverStatus Manager::readTemperature_handler(FwIndexType portNum, TemperatureSample& sample) {
    (void)portNum;
    if (!this->acquireForControl(Operation::READ_TEMPERATURE)) {
        return DriverStatus::BUSY;
    }
    GateHold hold(this->m_gate, true);
    Temperature coreTemperature;
    const Status status = this->m_device.readTemperature(coreTemperature);
    if (status == Status::OK) {
        sample.set(coreTemperature.raw, coreTemperature.celsius);
    } else {
        this->recordStatus(Operation::READ_TEMPERATURE, status);
    }
    return Manager::toFppStatus(status);
}

void Manager::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    // No gate: the queue and counters are lock-free, and F Prime keeps event
    // throttle counters in atomics, so publishing never costs the 4 kHz path a
    // sample. At most one queue's worth of edges per call.
    bool changed = this->m_highRateReports.takeDropped() > 0U;
    Status status = Status::OK;
    for (std::size_t edge = 0U; (edge < HIGH_RATE_REPORT_CAPACITY) && this->m_highRateReports.pop(status); ++edge) {
        changed = true;
        if (status != Status::OK) {
            this->log_WARNING_HI_OperationFailed(Operation::READ_SAMPLE, Manager::toFppStatus(status));
        }
    }
    if (changed) {
        this->tlmWrite_LastStatus(Manager::toFppStatus(this->m_publishedHighRateStatus.load()));
        this->tlmWrite_Errors(this->m_errors.load());
    }
}

DriverStatus Manager::measurementControl_handler(FwIndexType portNum, bool enable) {
    (void)portNum;
    const Operation operation = enable ? Operation::START : Operation::STOP;
    if (!this->acquireForControl(operation)) {
        return DriverStatus::BUSY;
    }
    GateHold hold(this->m_gate, true);
    const Status status = enable ? this->m_device.startMeasurement() : this->m_device.stopMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(enable);
    }
    this->recordStatus(operation, status);
    this->publishLifecycle();
    return Manager::toFppStatus(status);
}

void Manager::INITIALIZE_SYNCED_cmdHandler(FwOpcodeType opCode,
                                           U32 commandSequence,
                                           const SensorRange& rangeValue,
                                           const OutputDataRate& outputDataRate,
                                           const HighPassCorner& highPassCorner) {
    if (!this->acquireForControl(Operation::INITIALIZE)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    Configuration configuration = Configuration::externalSynchronized4kHz();
    configuration.range = static_cast<Range>(rangeValue.e);
    configuration.outputDataRate = static_cast<OdrSetting>(outputDataRate.e);
    configuration.highPassCorner = static_cast<HpfSetting>(highPassCorner.e);
    this->respond(opCode, commandSequence, this->initializeDevice(configuration));
}

void Manager::START_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    if (!this->acquireForControl(Operation::START)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    const Status status = this->m_device.startMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(true);
    }
    this->runControl(opCode, commandSequence, Operation::START, status);
}

void Manager::STOP_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    if (!this->acquireForControl(Operation::STOP)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    const Status status = this->m_device.stopMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(false);
    }
    this->runControl(opCode, commandSequence, Operation::STOP, status);
}

void Manager::SET_RANGE_cmdHandler(FwOpcodeType opCode, U32 commandSequence, const SensorRange& rangeValue) {
    if (!this->acquireForControl(Operation::SET_RANGE)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_RANGE,
                     this->m_device.setRange(static_cast<Range>(rangeValue.e)));
}

void Manager::SET_FILTER_cmdHandler(FwOpcodeType opCode,
                                    U32 commandSequence,
                                    const OutputDataRate& outputDataRate,
                                    const HighPassCorner& highPassCorner) {
    if (!this->acquireForControl(Operation::SET_FILTER)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_FILTER,
                     this->m_device.setFilter(static_cast<OdrSetting>(outputDataRate.e),
                                              static_cast<HpfSetting>(highPassCorner.e)));
}

void Manager::SET_SYNC_cmdHandler(FwOpcodeType opCode,
                                  U32 commandSequence,
                                  const SynchronizationMode& synchronizationMode,
                                  bool externalClock) {
    if (!this->acquireForControl(Operation::SET_SYNCHRONIZATION)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_SYNCHRONIZATION,
                     this->m_device.setSynchronization(static_cast<SyncMode>(synchronizationMode.e),
                                                       externalClock));
}

void Manager::SET_OFFSETS_cmdHandler(FwOpcodeType opCode, U32 commandSequence, I16 x, I16 y, I16 z) {
    if (!this->acquireForControl(Operation::SET_OFFSETS)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_OFFSETS, this->m_device.setOffsets(x, y, z));
}

void Manager::SET_ACTIVITY_cmdHandler(FwOpcodeType opCode,
                                      U32 commandSequence,
                                      U8 axisMask,
                                      U16 threshold,
                                      U8 count) {
    if (!this->acquireForControl(Operation::SET_ACTIVITY)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_ACTIVITY,
                     this->m_device.configureActivity(axisMask, threshold, count));
}

void Manager::SET_INTERRUPTS_cmdHandler(FwOpcodeType opCode,
                                        U32 commandSequence,
                                        U8 interruptMap,
                                        bool activeHigh) {
    if (!this->acquireForControl(Operation::SET_INTERRUPTS)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_INTERRUPTS,
                     this->m_device.configureInterrupts(interruptMap, activeHigh));
}

void Manager::SET_SELF_TEST_cmdHandler(FwOpcodeType opCode,
                                       U32 commandSequence,
                                       bool enableMode,
                                       bool applyForce) {
    if (!this->acquireForControl(Operation::SET_SELF_TEST)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    const Status status = this->m_device.setSelfTest(enableMode, applyForce);
    if (status == Status::OK) {
        this->log_ACTIVITY_HI_SelfTestState(enableMode, applyForce);
    }
    this->runControl(opCode, commandSequence, Operation::SET_SELF_TEST, status);
}

void Manager::SET_FIFO_WATERMARK_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 watermark) {
    if (!this->acquireForControl(Operation::SET_FIFO_WATERMARK)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->runControl(opCode, commandSequence, Operation::SET_FIFO_WATERMARK,
                     this->m_device.setFifoWatermark(watermark));
}

void Manager::READ_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    if (!this->acquireForControl(Operation::READ_SAMPLE)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    if (!this->m_device.isMeasuring()) {
        this->recordStatus(Operation::READ_SAMPLE, Status::INVALID_STATE);
        this->respond(opCode, commandSequence, Status::INVALID_STATE);
        return;
    }
    // A STATUS read here would clear DATA_RDY and make the next acquisition
    // read of this sensor stale, so the snapshot reads only the data registers.
    RawSample raw;
    Status status = this->m_device.readRawSample(raw);
    if (status != Status::OK) {
        this->recordStatus(Operation::READ_SAMPLE, status);
        this->respond(opCode, commandSequence, status);
        return;
    }
    Sample sample;
    this->m_device.scale(raw, sample);
    ++this->m_samplesRead;
    this->emitSampleTelemetry(sample);

    Temperature temperature;
    status = this->m_device.readTemperature(temperature);
    if (status != Status::OK) {
        this->recordStatus(Operation::READ_TEMPERATURE, status);
        this->respond(opCode, commandSequence, status);
        return;
    }
    this->tlmWrite_TemperatureCelsius(temperature.celsius);
    this->tlmWrite_StatusRegister(this->m_device.lastStatusRegister());
    this->tlmWrite_SamplesRead(this->m_samplesRead);
    this->tlmWrite_Errors(this->m_errors.load());
    this->tlmWrite_LastStatus(DriverStatus::OK);
    this->publishLifecycle();
    this->respond(opCode, commandSequence, Status::OK);
}

void Manager::CLEAR_EVENT_THROTTLE_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    if (!this->acquireForControl(Operation::CLEAR_EVENT_THROTTLE)) {
        this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    GateHold hold(this->m_gate, true);
    this->log_WARNING_HI_OperationFailed_ThrottleClear();
    this->cmdResponse_out(opCode, commandSequence, Fw::CmdResponse::OK);
}

bool Manager::acquireForControl(Operation operation) {
    if (this->m_gate.acquire(this->m_delay, CONTROL_ACQUIRE_ATTEMPTS)) {
        return true;
    }
    // ControlBusy is not throttled, so emitting it without the gate touches no
    // state shared with the gate owner.
    this->log_WARNING_LO_ControlBusy(operation);
    return false;
}

void Manager::runControl(FwOpcodeType opCode, U32 commandSequence, Operation operation, Status status) {
    this->recordStatus(operation, status);
    this->publishLifecycle();
    this->respond(opCode, commandSequence, status);
}

Status Manager::initializeDevice(const Configuration& configuration) {
    const Status status = this->m_device.initialize(configuration);
    if (status == Status::OK) {
        this->log_WARNING_HI_OperationFailed_ThrottleClear();
        this->m_lastHighRateStatus = Status::OK;
        this->m_publishedHighRateStatus.store(Status::OK);
        this->log_ACTIVITY_HI_InitializationComplete(this->m_device.identity().revisionId);
        this->tlmWrite_LastStatus(DriverStatus::OK);
    } else {
        this->recordStatus(Operation::INITIALIZE, status);
    }
    this->publishLifecycle();
    return status;
}

DriverStatus Manager::toFppStatus(Status status) {
    switch (status) {
        case Status::OK:
            return DriverStatus::OK;
        case Status::PARTIAL:
            return DriverStatus::PARTIAL;
        case Status::INVALID_ARGUMENT:
            return DriverStatus::INVALID_ARGUMENT;
        case Status::INVALID_CONFIGURATION:
            return DriverStatus::INVALID_CONFIGURATION;
        case Status::INVALID_STATE:
            return DriverStatus::INVALID_STATE;
        case Status::SPI_ERROR:
            return DriverStatus::SPI_ERROR;
        case Status::IDENTITY_MISMATCH:
            return DriverStatus::IDENTITY_MISMATCH;
        case Status::TIMEOUT:
            return DriverStatus::TIMEOUT;
        case Status::VERIFY_ERROR:
            return DriverStatus::VERIFY_ERROR;
        case Status::FIFO_MISALIGNED:
            return DriverStatus::FIFO_MISALIGNED;
        case Status::FIFO_EMPTY:
            return DriverStatus::FIFO_EMPTY;
        case Status::CAPACITY_EXCEEDED:
            return DriverStatus::CAPACITY_EXCEEDED;
        case Status::BUSY:
            return DriverStatus::BUSY;
        case Status::STALE_DATA:
            return DriverStatus::STALE_DATA;
        case Status::IMPLAUSIBLE_DATA:
            return DriverStatus::IMPLAUSIBLE_DATA;
        case Status::NVM_MISMATCH:
            return DriverStatus::NVM_MISMATCH;
        case Status::FROZEN_DATA:
            return DriverStatus::FROZEN_DATA;
    }
    // Unreachable: every core status has an FPP mapping.
    FW_ASSERT(0, static_cast<FwAssertArgType>(status));
    return DriverStatus::INVALID_STATE;
}

Configuration Manager::toCoreConfiguration(const DeviceConfiguration& configuration) {
    Configuration core;
    core.range = static_cast<Range>(configuration.get_range());
    core.outputDataRate = static_cast<OdrSetting>(configuration.get_outputDataRate());
    core.highPassCorner = static_cast<HpfSetting>(configuration.get_highPassCorner());
    core.syncMode = static_cast<SyncMode>(configuration.get_synchronizationMode());
    core.externalClock = configuration.get_externalClock();
    core.temperatureEnabled = configuration.get_temperatureEnabled();
    core.dataReadyEnabled = configuration.get_dataReadyEnabled();
    core.interruptActiveHigh = configuration.get_interruptActiveHigh();
    core.interruptMap = configuration.get_interruptMap();
    core.fifoWatermark = configuration.get_fifoWatermark();
    return core;
}

bool Manager::isOperatorError(Status status) {
    // Rejected arguments and out-of-sequence commands are reported by the
    // command response; they are not evidence of a device fault.
    return (status == Status::INVALID_ARGUMENT) || (status == Status::INVALID_CONFIGURATION) ||
           (status == Status::INVALID_STATE);
}

void Manager::recordStatus(Operation operation, Status status) {
    const DriverStatus fppStatus = Manager::toFppStatus(status);
    this->tlmWrite_LastStatus(fppStatus);
    if ((status == Status::OK) || Manager::isOperatorError(status)) {
        return;
    }
    this->tlmWrite_Errors(this->m_errors.fetch_add(1U) + 1U);
    this->log_WARNING_HI_OperationFailed(operation, fppStatus);
}

void Manager::recordHighRateStatus(Status status) {
    if (status != Status::OK) {
        this->m_errors.fetch_add(1U);
    }
    if (status == this->m_lastHighRateStatus) {
        return;
    }
    // Edge-reported: schedIn emits the (throttled) event and telemetry off
    // this path, so an intermittent sensor can neither flood the event stream
    // nor stretch the frame.
    this->m_lastHighRateStatus = status;
    this->m_publishedHighRateStatus.store(status);
    (void)this->m_highRateReports.push(status);
}

void Manager::respond(FwOpcodeType opCode, U32 commandSequence, Status status) {
    Fw::CmdResponse response = Fw::CmdResponse::EXECUTION_ERROR;
    if (status == Status::OK) {
        response = Fw::CmdResponse::OK;
    } else if ((status == Status::INVALID_ARGUMENT) || (status == Status::INVALID_CONFIGURATION)) {
        response = Fw::CmdResponse::VALIDATION_ERROR;
    }
    this->cmdResponse_out(opCode, commandSequence, response);
}

void Manager::publishLifecycle() {
    this->tlmWrite_Initialized(this->m_device.isInitialized() ? 1U : 0U);
    this->tlmWrite_Measuring(this->m_device.isMeasuring() ? 1U : 0U);
    this->tlmWrite_SelfTest(this->m_device.selfTestBits());
}

void Manager::emitSampleTelemetry(const Sample& sample) {
    this->tlmWrite_RawX(sample.raw.x);
    this->tlmWrite_RawY(sample.raw.y);
    this->tlmWrite_RawZ(sample.raw.z);
    this->tlmWrite_AccelerationX(sample.xMetersPerSecondSquared);
    this->tlmWrite_AccelerationY(sample.yMetersPerSecondSquared);
    this->tlmWrite_AccelerationZ(sample.zMetersPerSecondSquared);
}

}  // namespace Adxl355
