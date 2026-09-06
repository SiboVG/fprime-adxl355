#ifndef ADXL355_APPLICATION_SYNCHRONIZED_APPLICATION_HPP
#define ADXL355_APPLICATION_SYNCHRONIZED_APPLICATION_HPP

#include "Adxl355/Application/SynchronizedApplicationComponentAc.hpp"

namespace Adxl355 {

class SynchronizedApplication final : public SynchronizedApplicationComponentBase {
  public:
    explicit SynchronizedApplication(const char* const componentName);
    ~SynchronizedApplication();

    bool configure(U8 sensorCount);

  private:
    void acquireFrame_handler(FwIndexType portNum, U64 syncTimeMicroseconds) override;

    void INITIALIZE_ALL_cmdHandler(FwOpcodeType opCode,
                                   U32 commandSequence,
                                   SensorRange range,
                                   OutputDataRate outputDataRate,
                                   HighPassCorner highPassCorner) override;
    void SET_SENSOR_COUNT_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 sensorCount) override;
    void SET_ENABLED_cmdHandler(FwOpcodeType opCode, U32 commandSequence, bool enabled) override;
    void REPORT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void INITIALIZE_ALL_INTERNAL_cmdHandler(FwOpcodeType opCode,
                                            U32 commandSequence,
                                            SensorRange range,
                                            OutputDataRate outputDataRate,
                                            HighPassCorner highPassCorner) override;

    void initializeAll(FwOpcodeType opCode, U32 commandSequence, DeviceConfiguration& configuration);
    void reportTelemetry();

    U8 m_sensorCount;
    bool m_enabled;
    U64 m_sequence;
    U64 m_frames;
    U64 m_partialFrames;
    U8 m_lastValidMask;
    U8 m_initializedMask;
};

}  // namespace Adxl355

#endif
