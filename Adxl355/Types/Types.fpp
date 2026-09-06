module Adxl355 {

  constant MAX_SENSORS = 7

  @ Status returned by the device manager and register-protocol library
  enum DriverStatus {
    OK = 0
    PARTIAL = 1
    INVALID_ARGUMENT = 2
    INVALID_CONFIGURATION = 3
    INVALID_STATE = 4
    SPI_ERROR = 5
    IDENTITY_MISMATCH = 6
    TIMEOUT = 7
    VERIFY_ERROR = 8
    FIFO_MISALIGNED = 9
    FIFO_EMPTY = 10
    CAPACITY_EXCEEDED = 11
  }

  enum SensorRange {
    G2 = 1
    G4 = 2
    G8 = 3
  }

  enum OutputDataRate {
    HZ_4000 = 0
    HZ_2000 = 1
    HZ_1000 = 2
    HZ_500 = 3
    HZ_250 = 4
    HZ_125 = 5
    HZ_62_5 = 6
    HZ_31_25 = 7
    HZ_15_625 = 8
    HZ_7_813 = 9
    HZ_3_906 = 10
  }

  enum HighPassCorner {
    OFF = 0
    ODR_X_24_7E_MINUS_4 = 1
    ODR_X_6_2084E_MINUS_4 = 2
    ODR_X_1_5545E_MINUS_4 = 3
    ODR_X_0_3862E_MINUS_4 = 4
    ODR_X_0_0954E_MINUS_4 = 5
    ODR_X_0_0238E_MINUS_4 = 6
  }

  enum SynchronizationMode {
    INTERNAL = 0
    EXTERNAL_NO_INTERPOLATION = 1
    EXTERNAL_WITH_INTERPOLATION = 2
  }

  enum Operation {
    INITIALIZE
    START
    STOP
    READ_SAMPLE
    READ_TEMPERATURE
    READ_STATUS
    SET_RANGE
    SET_FILTER
    SET_SYNCHRONIZATION
    SET_OFFSETS
    SET_ACTIVITY
    SET_INTERRUPTS
    SET_SELF_TEST
    SET_FIFO_WATERMARK
  }

  struct DeviceConfiguration {
    range: SensorRange
    outputDataRate: OutputDataRate
    highPassCorner: HighPassCorner
    synchronizationMode: SynchronizationMode
    externalClock: bool
    temperatureEnabled: bool
    dataReadyEnabled: bool
    interruptActiveHigh: bool
    interruptMap: U8
    fifoWatermark: U8
  }

  struct AccelerationSample {
    rawX: I32
    rawY: I32
    rawZ: I32
    xMetersPerSecondSquared: F64
    yMetersPerSecondSquared: F64
    zMetersPerSecondSquared: F64
  }

  struct TemperatureSample {
    raw: U16
    celsius: F64
  }

  array AccelerationSampleArray = [MAX_SENSORS] AccelerationSample
  array DriverStatusArray = [MAX_SENSORS] DriverStatus

  struct SampleFrame {
    sequence: U64
    syncTimeMicroseconds: U64
    validMask: U8
    sensorCount: U8
    samples: AccelerationSampleArray
    sensorStatus: DriverStatusArray
  }

  @ Configure and initialize one ADXL355
  port Configure(ref configuration: DeviceConfiguration) -> DriverStatus

  @ Read one coherent X/Y/Z register burst
  port ReadAcceleration(ref sample: AccelerationSample) -> DriverStatus

  @ Read the non-double-buffered temperature register pair
  port ReadTemperature(ref sample: TemperatureSample) -> DriverStatus

  @ Start or stop measurement mode
  port MeasurementControl(enable: bool) -> DriverStatus

  @ Trigger acquisition for one FPGA-generated synchronization instant
  port AcquireFrame(syncTimeMicroseconds: U64)

  @ Publish one frame containing up to seven sequentially-read, time-aligned sensors
  port FrameReady(ref frame: SampleFrame)
}
