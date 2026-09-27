#ifndef ADXL355_MANAGER_MANAGER_HPP
#define ADXL355_MANAGER_MANAGER_HPP

#include "Adxl355/Device/AccessGate.hpp"
#include "Adxl355/Device/Device.hpp"
#include "Adxl355/Manager/ManagerComponentAc.hpp"
#include "Adxl355/Platform/OsDelay.hpp"

namespace Adxl355 {

class Manager final : public ManagerComponentBase {
  public:
    //! Attempts (1 ms apart) a control handler makes to take the gate. The
    //! high-rate reader holds it for one SPI transfer, so one retry suffices.
    static const U32 CONTROL_ACQUIRE_ATTEMPTS = 10U;

    explicit Manager(const char* const componentName);
    ~Manager();

  private:
    class PortSpiBus final : public SpiBus {
      public:
        explicit PortSpiBus(Manager& owner);
        bool transfer(const std::uint8_t* transmit, std::uint8_t* receive, std::size_t size) override;

      private:
        Manager& m_owner;
    };

    DriverStatus configure_handler(FwIndexType portNum, const DeviceConfiguration& configuration) override;
    DriverStatus readAcceleration_handler(FwIndexType portNum, AccelerationSample& sample) override;
    DriverStatus readTemperature_handler(FwIndexType portNum, TemperatureSample& sample) override;
    DriverStatus measurementControl_handler(FwIndexType portNum, bool enable) override;

    void INITIALIZE_SYNCED_cmdHandler(FwOpcodeType opCode,
                                      U32 commandSequence,
                                      const SensorRange& range,
                                      const OutputDataRate& outputDataRate,
                                      const HighPassCorner& highPassCorner) override;
    void START_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void STOP_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void SET_RANGE_cmdHandler(FwOpcodeType opCode, U32 commandSequence, const SensorRange& range) override;
    void SET_FILTER_cmdHandler(FwOpcodeType opCode,
                               U32 commandSequence,
                               const OutputDataRate& outputDataRate,
                               const HighPassCorner& highPassCorner) override;
    void SET_SYNC_cmdHandler(FwOpcodeType opCode,
                             U32 commandSequence,
                             const SynchronizationMode& synchronizationMode,
                             bool externalClock) override;
    void SET_OFFSETS_cmdHandler(FwOpcodeType opCode,
                                U32 commandSequence,
                                I16 x,
                                I16 y,
                                I16 z) override;
    void SET_ACTIVITY_cmdHandler(FwOpcodeType opCode,
                                 U32 commandSequence,
                                 U8 axisMask,
                                 U16 threshold,
                                 U8 count) override;
    void SET_INTERRUPTS_cmdHandler(FwOpcodeType opCode,
                                   U32 commandSequence,
                                   U8 interruptMap,
                                   bool activeHigh) override;
    void SET_SELF_TEST_cmdHandler(FwOpcodeType opCode,
                                  U32 commandSequence,
                                  bool enableMode,
                                  bool applyForce) override;
    void SET_FIFO_WATERMARK_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 watermark) override;
    void READ_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void CLEAR_EVENT_THROTTLE_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;

    bool acquireForControl(Operation operation);
    void runControl(FwOpcodeType opCode, U32 commandSequence, Operation operation, Status status);
    Status initializeDevice(const Configuration& configuration);
    static DriverStatus toFppStatus(Status status);
    static Configuration toCoreConfiguration(const DeviceConfiguration& configuration);
    static bool isOperatorError(Status status);
    void recordStatus(Operation operation, Status status);
    void recordHighRateStatus(Operation operation, Status status);
    void respond(FwOpcodeType opCode, U32 commandSequence, Status status);
    void publishLifecycle();
    void emitSampleTelemetry(const Sample& sample);

    PortSpiBus m_spiBus;
    OsDelay m_delay;
    Device m_device;
    AccessGate m_gate;
    U64 m_samplesRead;
    U64 m_errors;
    Status m_lastHighRateStatus;
};

}  // namespace Adxl355

#endif
