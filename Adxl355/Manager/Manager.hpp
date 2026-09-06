#ifndef ADXL355_MANAGER_MANAGER_HPP
#define ADXL355_MANAGER_MANAGER_HPP

#include "Adxl355/Device/Device.hpp"
#include "Adxl355/Manager/ManagerComponentAc.hpp"

namespace Adxl355 {

class Manager final : public ManagerComponentBase {
  public:
    explicit Manager(const char* const componentName);
    ~Manager();

  PRIVATE:
    class PortSpiBus final : public SpiBus {
      public:
        explicit PortSpiBus(Manager& owner);
        bool transfer(const std::uint8_t* transmit, std::uint8_t* receive, std::size_t size) override;

      private:
        Manager& m_owner;
    };

    class OsDelay final : public Delay {
      public:
        void delayMilliseconds(std::uint32_t milliseconds) override;
    };

    DriverStatus configure_handler(FwIndexType portNum, DeviceConfiguration& configuration) override;
    DriverStatus readAcceleration_handler(FwIndexType portNum, AccelerationSample& sample) override;
    DriverStatus readTemperature_handler(FwIndexType portNum, TemperatureSample& sample) override;
    DriverStatus measurementControl_handler(FwIndexType portNum, bool enable) override;

    void INITIALIZE_SYNCED_cmdHandler(FwOpcodeType opCode,
                                      U32 commandSequence,
                                      SensorRange range,
                                      OutputDataRate outputDataRate,
                                      HighPassCorner highPassCorner) override;
    void START_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void STOP_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void SET_RANGE_cmdHandler(FwOpcodeType opCode, U32 commandSequence, SensorRange range) override;
    void SET_FILTER_cmdHandler(FwOpcodeType opCode,
                               U32 commandSequence,
                               OutputDataRate outputDataRate,
                               HighPassCorner highPassCorner) override;
    void SET_SYNC_cmdHandler(FwOpcodeType opCode,
                             U32 commandSequence,
                             SynchronizationMode synchronizationMode,
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
                                  bool stimulateAxis1,
                                  bool stimulateAxis2) override;
    void SET_FIFO_WATERMARK_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 watermark) override;
    void READ_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;

    static DriverStatus toFppStatus(Status status);
    static Configuration toCoreConfiguration(const DeviceConfiguration& configuration);
    void recordStatus(Operation operation, Status status);
    void recordHighRateStatus(Operation operation, Status status);
    void respond(FwOpcodeType opCode, U32 commandSequence, Status status);
    void emitSampleTelemetry(const Sample& sample);

    PortSpiBus m_spiBus;
    OsDelay m_delay;
    Device m_device;
    U64 m_samplesRead;
    U64 m_errors;
    Status m_lastHighRateStatus;
};

}  // namespace Adxl355

#endif
