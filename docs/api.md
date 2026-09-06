# API reference

## Hardware-independent C++ API

Include `Adxl355/Device/Device.hpp`. Construct one `Device` per chip select with platform implementations of `SpiBus` and `Delay`.

| API | Behavior |
| --- | --- |
| `initialize(configuration)` | Validate identity, reset, configure with readback, and start measurement |
| `readIdentity` / `readStatus` | Read identity or decoded status flags |
| `startMeasurement` / `stopMeasurement` | Change `POWER_CTL.STANDBY` |
| `readSample` / `readRawSample` | One coherent nine-byte X/Y/Z burst, in raw counts or SI units |
| `readTemperature` | Read the 12-bit temperature result and convert to degrees Celsius |
| `readFifo` | Read whole XYZ sets and validate FIFO axis/empty markers |
| `setRange` | Select +/-2 g, +/-4 g, or +/-8 g |
| `setFilter` | Select ODR/low-pass and high-pass corner |
| `setSynchronization` | Select internal, external interpolated, or external non-interpolated sync and external clock |
| `setOffsets` | Program signed 16-bit X/Y/Z offsets |
| `configureActivity` | Configure axis mask, threshold, and count |
| `configureInterrupts` | Configure interrupt map and polarity with `INT2` clock-conflict validation |
| `setSelfTest` | Control the two self-test stimulus bits; evaluation of response is left to the application |
| `setFifoWatermark` | Configure 1 through 96 FIFO entries |
| `readRegister` / `writeRegister` | Checked low-level access for bring-up and extensions |

`Sample` reports acceleration in metres per second squared using typical sensitivities of 256,000, 128,000, and 64,000 LSB/g for the three ranges and standard gravity 9.80665 m/s^2. Calibration remains a mission-level responsibility.

## FPP ports

| Port | Direction and use |
| --- | --- |
| `Configure` | Application to manager; initialize one device from a complete typed configuration |
| `ReadAcceleration` | Application to manager; return one sample and `DriverStatus` |
| `ReadTemperature` | Client to manager; return one temperature and status |
| `MeasurementControl` | Client to manager; start or stop measurement |
| `AcquireFrame` | FPGA timing source to synchronized application; carries the sampling timestamp |
| `FrameReady` | Synchronized application to downstream processing; carries a fixed-capacity frame |
| `Drv.SpiReadWrite` | Manager to generic platform SPI driver |

## `Adxl355.Manager` commands

- `INITIALIZE_SYNCED(range, outputDataRate, highPassCorner)` resets and initializes one device with external clock and external synchronization without interpolation.
- `START` and `STOP` control measurement.
- `SET_RANGE`, `SET_FILTER`, and `SET_SYNC` change primary sampling configuration.
- `SET_OFFSETS`, `SET_ACTIVITY`, `SET_INTERRUPTS`, `SET_SELF_TEST`, and `SET_FIFO_WATERMARK` expose the remaining controls.
- `READ_SNAPSHOT` reads acceleration, temperature, and status and publishes low-rate telemetry.

The synchronous high-rate `readAcceleration` port does not emit six acceleration channels on every call. It increments an internal counter; `READ_SNAPSHOT` is the intentional telemetry/downsampling point.

## `Adxl355.SynchronizedApplication` commands

- `INITIALIZE_ALL(range, outputDataRate, highPassCorner)` configures all connected managers with external clock and no-interpolation synchronization. `InitializedMask` and the command response indicate complete or partial initialization.
- `INITIALIZE_ALL_INTERNAL(range, outputDataRate, highPassCorner)` configures all connected managers to use their internal oscillators. It is intended for asynchronous bench bring-up and does not establish cross-sensor phase coherence.
- `SET_SENSOR_COUNT(count)` selects 1 through 7 managers.
- `SET_ENABLED(enabled)` gates acquisition callbacks without changing sensor power state.
- `REPORT` emits frame counters, partial-frame count, last valid mask, configured sensor count, enabled state, and initialized mask.

## Status values

`DriverStatus` distinguishes invalid arguments/configuration/state, SPI errors, identity mismatch, reset timeout, readback mismatch, FIFO misalignment/empty conditions, partial aggregate success, and capacity overflow. A frame is complete only when `validMask == (1 << sensorCount) - 1`.
