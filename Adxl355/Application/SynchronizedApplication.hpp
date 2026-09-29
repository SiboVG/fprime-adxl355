#ifndef ADXL355_APPLICATION_SYNCHRONIZED_APPLICATION_HPP
#define ADXL355_APPLICATION_SYNCHRONIZED_APPLICATION_HPP

#include "Adxl355/Application/SynchronizedApplicationComponentAc.hpp"
#include "Adxl355/Device/AccessGate.hpp"
#include "Adxl355/Device/ReportQueue.hpp"
#include "Adxl355/Platform/OsDelay.hpp"

#include <atomic>

namespace Adxl355 {

//! Execution context: acquireFrame runs on the timing thread, schedIn on a rate
//! group, commands on the command dispatcher thread. The frame body and every
//! reconfiguration run under m_gate. State read by REPORT, by schedIn or by the
//! trigger before it owns the gate is atomic or in the lock-free report queue;
//! everything else is touched only by the gate owner.
class SynchronizedApplication final : public SynchronizedApplicationComponentBase {
  public:
    //! Attempts (1 ms apart) a command makes to take the gate. A frame holds
    //! it for less than one 250 microsecond period.
    static const U32 CONTROL_ACQUIRE_ATTEMPTS = 10U;
    //! Frame edges held for schedIn; beyond this many between two schedIn calls
    //! the rest are dropped. PartialFrames still counts every partial frame.
    static const std::size_t FRAME_REPORT_CAPACITY = 16U;

    explicit SynchronizedApplication(const char* const componentName);
    ~SynchronizedApplication();

    //! Topology-time sensor count (1 through MAX_SENSORS); acquisition must be disabled.
    DriverStatus configure(U8 sensorCount);

  private:
    //! A partial frame whose valid mask differs from the previous frame's, or
    //! the first complete frame after partial ones.
    struct FrameReport {
        U64 sequence;
        U8 validMask;
        bool recovered;
    };

    void acquireFrame_handler(FwIndexType portNum, U64 syncTimeMicroseconds) override;
    void schedIn_handler(FwIndexType portNum, U32 context) override;

    void INITIALIZE_ALL_cmdHandler(FwOpcodeType opCode,
                                   U32 commandSequence,
                                   const SensorRange& range,
                                   const OutputDataRate& outputDataRate,
                                   const HighPassCorner& highPassCorner) override;
    void SET_SENSOR_COUNT_cmdHandler(FwOpcodeType opCode, U32 commandSequence, U8 sensorCount) override;
    void SET_ENABLED_cmdHandler(FwOpcodeType opCode, U32 commandSequence, bool enabled) override;
    void REPORT_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;
    void INITIALIZE_ALL_INTERNAL_cmdHandler(FwOpcodeType opCode,
                                            U32 commandSequence,
                                            const SensorRange& range,
                                            const OutputDataRate& outputDataRate,
                                            const HighPassCorner& highPassCorner) override;
    void CLEAR_EVENT_THROTTLE_cmdHandler(FwOpcodeType opCode, U32 commandSequence) override;

    void initializeAll(FwOpcodeType opCode, U32 commandSequence, const DeviceConfiguration& configuration);
    void rejectBusy(FwOpcodeType opCode, U32 commandSequence);
    void rejectWhileEnabled(FwOpcodeType opCode, U32 commandSequence);
    void clearEventThrottles();
    void reportTelemetry();

    OsDelay m_delay;
    AccessGate m_gate;
    std::atomic<bool> m_enabled;
    std::atomic<U8> m_sensorCount;
    std::atomic<U8> m_initializedMask;
    std::atomic<U8> m_lastValidMask;
    std::atomic<U64> m_sequence;
    std::atomic<U64> m_frames;
    std::atomic<U64> m_partialFrames;
    std::atomic<U64> m_skippedTriggers;
    ReportQueue<FrameReport, FRAME_REPORT_CAPACITY> m_frameReports;
};

}  // namespace Adxl355

#endif
