#include "Adxl355/Manager/Manager.hpp"

#include "Fw/Buffer/Buffer.hpp"
#include "Fw/Time/TimeInterval.hpp"
#include "Os/Task.hpp"

namespace Adxl355 {

Manager::PortSpiBus::PortSpiBus(Manager& owner) : m_owner(owner) {}

bool Manager::PortSpiBus::transfer(const std::uint8_t* transmit,
                                   std::uint8_t* receive,
                                   std::size_t size) {
    if ((transmit == nullptr) || (receive == nullptr) || (size == 0U) ||
        !this->m_owner.isConnected_spi_OutputPort(0)) {
        return false;
    }
    Fw::Buffer writeBuffer(const_cast<U8*>(reinterpret_cast<const U8*>(transmit)), static_cast<U32>(size));
    Fw::Buffer readBuffer(reinterpret_cast<U8*>(receive), static_cast<U32>(size));
    this->m_owner.spi_out(0, writeBuffer, readBuffer);
    return true;
}

void Manager::OsDelay::delayMilliseconds(std::uint32_t milliseconds) {
    const U32 seconds = static_cast<U32>(milliseconds / 1000U);
    const U32 microseconds = static_cast<U32>((milliseconds % 1000U) * 1000U);
    (void)Os::Task::delay(Fw::TimeInterval(seconds, microseconds));
}

Manager::Manager(const char* const componentName)
    : ManagerComponentBase(componentName),
      m_spiBus(*this),
      m_delay(),
      m_device(this->m_spiBus, this->m_delay),
      m_samplesRead(0U),
      m_errors(0U),
      m_lastHighRateStatus(Status::OK) {}

Manager::~Manager() {}

DriverStatus Manager::configure_handler(FwIndexType portNum, DeviceConfiguration& configuration) {
    (void)portNum;
    const Status status = this->m_device.initialize(Manager::toCoreConfiguration(configuration));
    if (status == Status::OK) {
        Identity identity;
        const Status identityStatus = this->m_device.readIdentity(identity);
        if (identityStatus != Status::OK) {
            this->recordStatus(Operation::INITIALIZE, identityStatus);
            return Manager::toFppStatus(identityStatus);
        }
        this->log_ACTIVITY_HI_InitializationComplete(identity.revisionId);
        this->tlmWrite_Initialized(1U);
        this->tlmWrite_Measuring(1U);
        this->tlmWrite_LastStatus(DriverStatus::OK);
        this->m_lastHighRateStatus = Status::OK;
    } else {
        this->tlmWrite_Initialized(0U);
        this->recordStatus(Operation::INITIALIZE, status);
    }
    return Manager::toFppStatus(status);
}

DriverStatus Manager::readAcceleration_handler(FwIndexType portNum, AccelerationSample& sample) {
    (void)portNum;
    if (!this->m_device.isInitialized() || !this->m_device.isMeasuring()) {
        this->recordHighRateStatus(Operation::READ_SAMPLE, Status::INVALID_STATE);
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
        this->recordHighRateStatus(Operation::READ_SAMPLE, Status::OK);
    } else {
        this->recordHighRateStatus(Operation::READ_SAMPLE, status);
    }
    return Manager::toFppStatus(status);
}

DriverStatus Manager::readTemperature_handler(FwIndexType portNum, TemperatureSample& sample) {
    (void)portNum;
    Temperature coreTemperature;
    const Status status = this->m_device.readTemperature(coreTemperature);
    if (status == Status::OK) {
        sample.set(coreTemperature.raw, coreTemperature.celsius);
    } else {
        this->recordStatus(Operation::READ_TEMPERATURE, status);
    }
    return Manager::toFppStatus(status);
}

DriverStatus Manager::measurementControl_handler(FwIndexType portNum, bool enable) {
    (void)portNum;
    const Status status = enable ? this->m_device.startMeasurement() : this->m_device.stopMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(enable);
        this->tlmWrite_Measuring(enable ? 1U : 0U);
        this->tlmWrite_LastStatus(DriverStatus::OK);
    } else {
        this->recordStatus(enable ? Operation::START : Operation::STOP, status);
    }
    return Manager::toFppStatus(status);
}

void Manager::INITIALIZE_SYNCED_cmdHandler(FwOpcodeType opCode,
                                           U32 commandSequence,
                                           SensorRange rangeValue,
                                           OutputDataRate outputDataRate,
                                           HighPassCorner highPassCorner) {
    Configuration configuration = Configuration::externalSynchronized4kHz();
    configuration.range = static_cast<Range>(rangeValue.e);
    configuration.outputDataRate = static_cast<OdrSetting>(outputDataRate.e);
    configuration.highPassCorner = static_cast<HpfSetting>(highPassCorner.e);
    const Status status = this->m_device.initialize(configuration);
    if (status == Status::OK) {
        Identity identity;
        const Status identityStatus = this->m_device.readIdentity(identity);
        if (identityStatus != Status::OK) {
            this->recordStatus(Operation::INITIALIZE, identityStatus);
            this->respond(opCode, commandSequence, identityStatus);
            return;
        }
        this->log_ACTIVITY_HI_InitializationComplete(identity.revisionId);
        this->tlmWrite_Initialized(1U);
        this->tlmWrite_Measuring(1U);
        this->tlmWrite_LastStatus(DriverStatus::OK);
        this->m_lastHighRateStatus = Status::OK;
    } else {
        this->tlmWrite_Initialized(0U);
        this->recordStatus(Operation::INITIALIZE, status);
    }
    this->respond(opCode, commandSequence, status);
}

void Manager::START_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    const Status status = this->m_device.startMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(true);
        this->tlmWrite_Measuring(1U);
        this->tlmWrite_LastStatus(DriverStatus::OK);
    } else {
        this->recordStatus(Operation::START, status);
    }
    this->respond(opCode, commandSequence, status);
}

void Manager::STOP_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    const Status status = this->m_device.stopMeasurement();
    if (status == Status::OK) {
        this->log_ACTIVITY_LO_MeasurementState(false);
        this->tlmWrite_Measuring(0U);
        this->tlmWrite_LastStatus(DriverStatus::OK);
    } else {
        this->recordStatus(Operation::STOP, status);
    }
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_RANGE_cmdHandler(FwOpcodeType opCode, U32 commandSequence, SensorRange rangeValue) {
    const Status status = this->m_device.setRange(static_cast<Range>(rangeValue.e));
    this->recordStatus(Operation::SET_RANGE, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_FILTER_cmdHandler(FwOpcodeType opCode,
                                    U32 commandSequence,
                                    OutputDataRate outputDataRate,
                                    HighPassCorner highPassCorner) {
    const Status status = this->m_device.setFilter(static_cast<OdrSetting>(outputDataRate.e),
                                                   static_cast<HpfSetting>(highPassCorner.e));
    this->recordStatus(Operation::SET_FILTER, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_SYNC_cmdHandler(FwOpcodeType opCode,
                                  U32 commandSequence,
                                  SynchronizationMode synchronizationMode,
                                  bool externalClock) {
    const Status status = this->m_device.setSynchronization(static_cast<SyncMode>(synchronizationMode.e),
                                                            externalClock);
    this->recordStatus(Operation::SET_SYNCHRONIZATION, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_OFFSETS_cmdHandler(FwOpcodeType opCode, U32 commandSequence, I16 x, I16 y, I16 z) {
    const Status status = this->m_device.setOffsets(x, y, z);
    this->recordStatus(Operation::SET_OFFSETS, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_ACTIVITY_cmdHandler(FwOpcodeType opCode,
                                      U32 commandSequence,
                                      U8 axisMask,
                                      U16 threshold,
                                      U8 count) {
    const Status status = this->m_device.configureActivity(axisMask, threshold, count);
    this->recordStatus(Operation::SET_ACTIVITY, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_INTERRUPTS_cmdHandler(FwOpcodeType opCode,
                                        U32 commandSequence,
                                        U8 interruptMap,
                                        bool activeHigh) {
    const Status status = this->m_device.configureInterrupts(interruptMap, activeHigh);
    this->recordStatus(Operation::SET_INTERRUPTS, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_SELF_TEST_cmdHandler(FwOpcodeType opCode,
                                       U32 commandSequence,
                                       bool stimulateAxis1,
                                       bool stimulateAxis2) {
    const Status status = this->m_device.setSelfTest(stimulateAxis1, stimulateAxis2);
    this->recordStatus(Operation::SET_SELF_TEST, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::SET_FIFO_WATERMARK_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 watermark) {
    const Status status = this->m_device.setFifoWatermark(watermark);
    this->recordStatus(Operation::SET_FIFO_WATERMARK, status);
    this->respond(opCode, commandSequence, status);
}

void Manager::READ_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) {
    if (!this->m_device.isInitialized() || !this->m_device.isMeasuring()) {
        this->recordStatus(Operation::READ_SAMPLE, Status::INVALID_STATE);
        this->respond(opCode, commandSequence, Status::INVALID_STATE);
        return;
    }
    Sample sample;
    Status status = this->m_device.readSample(sample);
    if (status != Status::OK) {
        this->recordStatus(Operation::READ_SAMPLE, status);
        this->respond(opCode, commandSequence, status);
        return;
    }
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

    StatusRegister statusRegister;
    status = this->m_device.readStatus(statusRegister);
    if (status != Status::OK) {
        this->recordStatus(Operation::READ_STATUS, status);
        this->respond(opCode, commandSequence, status);
        return;
    }
    this->tlmWrite_StatusRegister(statusRegister.raw);
    if (statusRegister.fifoOverrun) {
        this->log_WARNING_HI_FifoOverrun(statusRegister.raw);
    }
    this->tlmWrite_SamplesRead(this->m_samplesRead);
    this->tlmWrite_Errors(this->m_errors);
    this->tlmWrite_LastStatus(DriverStatus::OK);
    this->respond(opCode, commandSequence, Status::OK);
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
        default:
            return DriverStatus::INVALID_STATE;
    }
}

Configuration Manager::toCoreConfiguration(const DeviceConfiguration& configuration) {
    Configuration core;
    core.range = static_cast<Range>(configuration.getrange());
    core.outputDataRate = static_cast<OdrSetting>(configuration.getoutputDataRate());
    core.highPassCorner = static_cast<HpfSetting>(configuration.gethighPassCorner());
    core.syncMode = static_cast<SyncMode>(configuration.getsynchronizationMode());
    core.externalClock = configuration.getexternalClock();
    core.temperatureEnabled = configuration.gettemperatureEnabled();
    core.dataReadyEnabled = configuration.getdataReadyEnabled();
    core.interruptActiveHigh = configuration.getinterruptActiveHigh();
    core.interruptMap = configuration.getinterruptMap();
    core.fifoWatermark = configuration.getfifoWatermark();
    return core;
}

void Manager::recordStatus(Operation operation, Status status) {
    const DriverStatus fppStatus = Manager::toFppStatus(status);
    this->tlmWrite_LastStatus(fppStatus);
    if (status != Status::OK) {
        ++this->m_errors;
        this->tlmWrite_Errors(this->m_errors);
        this->log_WARNING_HI_OperationFailed(operation, fppStatus);
    }
}

void Manager::recordHighRateStatus(Operation operation, Status status) {
    if (status == Status::OK) {
        if (this->m_lastHighRateStatus != Status::OK) {
            this->tlmWrite_LastStatus(DriverStatus::OK);
        }
        this->m_lastHighRateStatus = Status::OK;
        return;
    }

    ++this->m_errors;
    if (status != this->m_lastHighRateStatus) {
        const DriverStatus fppStatus = Manager::toFppStatus(status);
        this->tlmWrite_LastStatus(fppStatus);
        this->log_WARNING_HI_OperationFailed(operation, fppStatus);
    }
    this->m_lastHighRateStatus = status;
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

void Manager::emitSampleTelemetry(const Sample& sample) {
    this->tlmWrite_RawX(sample.raw.x);
    this->tlmWrite_RawY(sample.raw.y);
    this->tlmWrite_RawZ(sample.raw.z);
    this->tlmWrite_AccelerationX(sample.xMetersPerSecondSquared);
    this->tlmWrite_AccelerationY(sample.yMetersPerSecondSquared);
    this->tlmWrite_AccelerationZ(sample.zMetersPerSecondSquared);
}

}  // namespace Adxl355
