module Adxl355 {

  @ Application layer that reads all managers after an FPGA synchronization instant.
  passive component SynchronizedApplication {

    sync input port acquireFrame: Adxl355.AcquireFrame
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

    event SensorCountChanged(sensorCount: U8) severity activity high \
      format "ADXL355 synchronized sensor count set to {}"
    event AcquisitionStateChanged(enabled: bool) severity activity low \
      format "ADXL355 synchronized acquisition enabled: {}"
    event ArrayInitializationComplete(initializedMask: U8) severity activity high \
      format "ADXL355 array initialized; mask 0x{x}"
    event ArrayInitializationIncomplete(initializedMask: U8, expectedMask: U8) severity warning high \
      format "ADXL355 array initialization incomplete; mask 0x{x}, expected 0x{x}"
    event FrameIncomplete(sequence: U64, validMask: U8) severity warning high \
      format "ADXL355 frame {} incomplete; valid mask 0x{x}"
    event FrameRecovered(sequence: U64) severity activity high \
      format "ADXL355 frame {} restored all sensors"

    telemetry Frames: U64
    telemetry PartialFrames: U64
    telemetry LastValidMask: U8
    telemetry SensorCount: U8
    telemetry Enabled: U8
    telemetry InitializedMask: U8

    time get port timeCaller
    command reg port cmdRegOut
    command recv port cmdIn
    command resp port cmdResponseOut
    text event port logTextOut
    event port logOut
    telemetry port tlmOut
  }
}
