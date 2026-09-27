module Adxl355 {

  @ Device-manager layer for one ADXL355. The SPI bus driver remains device agnostic.
  @ Ports and commands are sync: the acquisition thread calls readAcceleration
  @ while commands arrive on the dispatcher thread. A non-blocking AccessGate
  @ serializes them; readAcceleration returns BUSY instead of waiting, and every
  @ other handler waits at most CONTROL_ACQUIRE_ATTEMPTS milliseconds.
  passive component Manager {

    sync input port configure: Adxl355.Configure
    sync input port readAcceleration: Adxl355.ReadAcceleration
    sync input port readTemperature: Adxl355.ReadTemperature
    sync input port measurementControl: Adxl355.MeasurementControl

    @ Standard F Prime SPI abstraction; connect one instance per chip-select.
    output port spi: Drv.SpiReadWrite

    sync command INITIALIZE_SYNCED(
      range: Adxl355.SensorRange
      outputDataRate: Adxl355.OutputDataRate
      highPassCorner: Adxl355.HighPassCorner
    )

    sync command START
    sync command STOP
    sync command SET_RANGE(range: Adxl355.SensorRange)
    sync command SET_FILTER(
      outputDataRate: Adxl355.OutputDataRate
      highPassCorner: Adxl355.HighPassCorner
    )
    sync command SET_SYNC(
      synchronizationMode: Adxl355.SynchronizationMode
      externalClock: bool
    )
    sync command SET_OFFSETS(x: I16, y: I16, z: I16)
    sync command SET_ACTIVITY(axisMask: U8, threshold: U16, count: U8)
    sync command SET_INTERRUPTS(interruptMap: U8, activeHigh: bool)
    @ ST1 enables self-test mode; ST2 applies the electrostatic force and requires ST1.
    sync command SET_SELF_TEST(enableMode: bool, applyForce: bool)
    sync command SET_FIFO_WATERMARK(watermark: U8)
    @ Diagnostic snapshot. Reads X/Y/Z without STATUS so it cannot consume the
    @ acquisition path's DATA_RDY; the published status is the last one observed.
    sync command READ_SNAPSHOT
    @ Re-arm throttled warning events after an operator has seen the fault.
    sync command CLEAR_EVENT_THROTTLE

    event InitializationComplete(revision: U8) severity activity high \
      format "ADXL355 initialized; revision 0x{x}"
    event MeasurementState(enabled: bool) severity activity low \
      format "ADXL355 measurement enabled: {}"
    event OperationFailed(
      operation: Adxl355.Operation
      status: Adxl355.DriverStatus
    ) severity warning high format "ADXL355 operation {} failed with {}" throttle 10
    event SelfTestState(enableMode: bool, applyForce: bool) severity activity high \
      format "ADXL355 self-test mode {} force {}"
    event ControlBusy(operation: Adxl355.Operation) severity warning low \
      format "ADXL355 operation {} rejected: manager busy"

    telemetry Initialized: U8
    telemetry Measuring: U8
    telemetry LastStatus: Adxl355.DriverStatus
    @ STATUS byte from the most recent status read (acquisition or reset path)
    telemetry StatusRegister: U8
    telemetry RawX: I32
    telemetry RawY: I32
    telemetry RawZ: I32
    telemetry AccelerationX: F64
    telemetry AccelerationY: F64
    telemetry AccelerationZ: F64
    telemetry TemperatureCelsius: F64
    telemetry SamplesRead: U64
    telemetry Errors: U64
    @ SELF_TEST bits: 0x01 mode (ST1), 0x02 force (ST2)
    telemetry SelfTest: U8

    time get port timeCaller
    command reg port cmdRegOut
    command recv port cmdIn
    command resp port cmdResponseOut
    text event port logTextOut
    event port logOut
    telemetry port tlmOut
  }
}
