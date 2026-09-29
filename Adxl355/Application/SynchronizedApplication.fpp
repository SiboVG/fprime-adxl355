module Adxl355 {

  @ Application layer that reads all managers after an FPGA synchronization instant.
  @ acquireFrame runs on the timing thread and never waits: if a command owns
  @ the application gate the trigger is skipped, counted, and still consumes a
  @ sequence number. Reconfiguration commands require acquisition disabled.
  @ acquireFrame emits no events: it queues frame edges, and schedIn publishes
  @ them from a rate group.
  passive component SynchronizedApplication {

    sync input port acquireFrame: Adxl355.AcquireFrame
    @ Publishes acquireFrame's edges as FrameIncomplete and FrameRecovered events.
    @ Connect to a rate group; it never takes the gate, so it cannot cost a frame.
    sync input port schedIn: Svc.Sched
    output port configureSensor: [Adxl355.MAX_SENSORS] Adxl355.Configure
    output port readSensor: [Adxl355.MAX_SENSORS] Adxl355.ReadAcceleration
    output port frameReady: Adxl355.FrameReady

    sync command INITIALIZE_ALL(
      range: Adxl355.SensorRange
      outputDataRate: Adxl355.OutputDataRate
      highPassCorner: Adxl355.HighPassCorner
    )
    sync command SET_SENSOR_COUNT(sensorCount: U8)
    sync command SET_ENABLED(enabled: bool)
    sync command REPORT

    @ Bench initialization using the sensor's internal clock and ODR.
    sync command INITIALIZE_ALL_INTERNAL(
      range: Adxl355.SensorRange
      outputDataRate: Adxl355.OutputDataRate
      highPassCorner: Adxl355.HighPassCorner
    )

    @ Re-arm throttled frame events.
    sync command CLEAR_EVENT_THROTTLE

    event SensorCountChanged(sensorCount: U8) severity activity high \
      format "ADXL355 synchronized sensor count set to {}"
    event AcquisitionStateChanged(enabled: bool) severity activity low \
      format "ADXL355 synchronized acquisition enabled: {}"
    event ArrayInitializationComplete(initializedMask: U8) severity activity high \
      format "ADXL355 array initialized; mask 0x{x}"
    event ArrayInitializationIncomplete(initializedMask: U8, expectedMask: U8) severity warning high \
      format "ADXL355 array initialization incomplete; mask 0x{x}, expected 0x{x}"
    event FrameIncomplete(sequence: U64, validMask: U8) severity warning high \
      format "ADXL355 frame {} incomplete; valid mask 0x{x}" throttle 10
    event FrameRecovered(sequence: U64) severity activity high \
      format "ADXL355 frame {} restored all sensors" throttle 10
    event ReconfigurationRejected severity warning low \
      format "ADXL355 reconfiguration rejected: disable acquisition first"
    event ApplicationBusy severity warning low \
      format "ADXL355 application command rejected: frame in progress"

    telemetry Frames: U64
    telemetry PartialFrames: U64
    telemetry LastValidMask: U8
    telemetry SensorCount: U8
    telemetry Enabled: U8
    telemetry InitializedMask: U8
    @ Enabled triggers dropped because the previous frame or a command owned the gate
    telemetry SkippedTriggers: U64

    time get port timeCaller
    command reg port cmdRegOut
    command recv port cmdIn
    command resp port cmdResponseOut
    text event port logTextOut
    event port logOut
    telemetry port tlmOut
  }
}
