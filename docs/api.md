# API reference

## Hardware-independent C++ API

Include `Adxl355/Device/Device.hpp`. Construct one `Device` per chip select with platform implementations of `SpiBus` and `Delay`.

| API | Behavior |
| --- | --- |
| `initialize(configuration)` | Validate, identify, reset with the NVM shadow-register check, configure with readback, and start measurement. All or nothing once the reset is issued. |
| `readIdentity` / `readStatus` | Read identity or decoded status flags. A STATUS read clears `DATA_RDY`, `FIFO_OVR`, and activity. |
| `softReset` | Capture the shadow reference if not yet captured, reset, poll `NVM_BUSY`, and repeat up to three times until the shadow registers match |
| `startMeasurement` / `stopMeasurement` | Change `POWER_CTL.STANDBY`; start requires a verified configuration, stop is always allowed |
| `readSample` | Status-checked acquisition read: one STATUS..ZDATA1 burst; `STALE_DATA` when no newer sample exists, `IMPLAUSIBLE_DATA` for impossible status bytes; output written only on `OK` |
| `readRawSample` | Diagnostic nine-byte X/Y/Z read without STATUS: no freshness check and no side effects |
| `scale(raw, sample)` | Convert raw counts to SI units for the configured range |
| `readTemperature` | Read the 12-bit temperature until two consecutive reads agree, then convert to degrees Celsius; `IMPLAUSIBLE_DATA` when TEMP2's reserved bits are set or the value lies outside −55 °C to +150 °C |
| `readFifo` | Read whole XYZ sets and validate FIFO entry count, axis markers, and empty markers |
| `setRange` | Select +/-2 g, +/-4 g, or +/-8 g |
| `setFilter` | Select ODR/low-pass and high-pass corner |
| `setSynchronization` | Select internal, external interpolated, or external non-interpolated sync and external clock; `EXT_SYNC = 01` requires the external clock |
| `setOffsets` | Program signed 16-bit X/Y/Z offsets |
| `configureActivity` | Configure axis mask, threshold, and count |
| `configureInterrupts` | Configure interrupt map and polarity with `INT2` clock-conflict validation |
| `setSelfTest(enableMode, applyForce)` | Set ST1 (self-test mode) and ST2 (force, requires ST1); tracked by `selfTestBits()` and cleared by reset. Evaluation of the response is left to the application. |
| `setFifoWatermark` | Configure 1 through 96 FIFO entries |
| `readRegister` / `writeRegister` | Checked low-level access for bring-up and extensions. A diagnostic write returns the lifecycle to `UNCONFIGURED`. |
| `state()` / `isInitialized()` / `isMeasuring()` | Lifecycle: `UNCONFIGURED`, `STANDBY`, or `MEASURING` |
| `identity()` / `lastStatusRegister()` | Identity read during the last successful initialization; last STATUS byte observed |

Runtime controls are rejected with `INVALID_STATE` while the device is `UNCONFIGURED`. `Sample` reports acceleration in metres per second squared using typical sensitivities of 256,000, 128,000, and 64,000 LSB/g for the three ranges (data sheet Table 2) and standard gravity 9.80665 m/s^2. Calibration remains a mission-level responsibility.

`Adxl355/Device/AccessGate.hpp` provides the non-blocking single-owner gate the F Prime components use. `tryAcquire` never waits; `acquire(delay, attempts)` retries 1 ms apart.

`Adxl355/Device/ReportQueue.hpp` provides the fixed-capacity single-producer, single-consumer queue that carries fault edges from the acquisition path to `schedIn`. `push` and `pop` never wait; a full queue drops the record and counts it (`takeDropped`).

## FPP ports

| Port | Direction and use |
| --- | --- |
| `Configure` | Application to manager; initialize one device from a complete typed configuration passed by value |
| `ReadAcceleration` | Application to manager; return one status-checked sample and `DriverStatus`; never blocks |
| `ReadTemperature` | Client to manager; return one consistent temperature and status |
| `MeasurementControl` | Client to manager; start or stop measurement |
| `AcquireFrame` | FPGA timing source to synchronized application; carries the sampling timestamp |
| `FrameReady` | Synchronized application to downstream processing; carries a fixed-capacity frame |
| `Drv.SpiWriteRead` | Manager to generic platform SPI driver; returns `Drv.SpiStatus`, and any failure becomes `SPI_ERROR` |
| `Svc.Sched` (`schedIn`) | Rate group to the manager and to the application; publishes the fault edges the acquisition path queued; never takes the gate |

## `Adxl355.Manager` commands

- `INITIALIZE_SYNCED(range, outputDataRate, highPassCorner)` resets and initializes one device with external clock and external synchronization without interpolation.
- `START` and `STOP` control measurement.
- `SET_RANGE`, `SET_FILTER`, and `SET_SYNC` change primary sampling configuration.
- `SET_OFFSETS`, `SET_ACTIVITY`, `SET_INTERRUPTS`, `SET_SELF_TEST(enableMode, applyForce)`, and `SET_FIFO_WATERMARK` expose the remaining controls.
- `READ_SNAPSHOT` reads acceleration and temperature without reading STATUS, and publishes low-rate telemetry including the last observed STATUS byte.
- `CLEAR_EVENT_THROTTLE` re-arms the throttled `OperationFailed` event.

Every command waits at most 10 ms for the manager's gate and otherwise responds `EXECUTION_ERROR` with a `ControlBusy` event. Invalid arguments respond `VALIDATION_ERROR`; they and out-of-sequence commands do not increment `Errors` or emit `OperationFailed`.

The synchronous high-rate `readAcceleration` port emits no events or telemetry. It increments internal counters and queues status edges; `schedIn` publishes them as `OperationFailed` (one per failure edge, throttled), followed by `LastStatus` (the current status) and `Errors`. `READ_SNAPSHOT` is the intentional telemetry/downsampling point for samples. `Initialized`, `Measuring`, and `SelfTest` telemetry is republished after every state-changing operation.

## `Adxl355.SynchronizedApplication` commands

- `INITIALIZE_ALL(range, outputDataRate, highPassCorner)` configures all connected managers with external clock and no-interpolation synchronization. `InitializedMask` and the command response indicate complete or partial initialization.
- `INITIALIZE_ALL_INTERNAL(range, outputDataRate, highPassCorner)` configures all connected managers to use their internal oscillators. It is intended for asynchronous bench bring-up and does not establish cross-sensor phase coherence.
- `SET_SENSOR_COUNT(count)` selects 1 through 7 managers.
- `SET_ENABLED(enabled)` gates acquisition callbacks without changing sensor power state. Enabling restarts edge detection and clears the event throttles; the disable response is sent only after any frame in progress has finished.
- `REPORT` emits frame counters, partial-frame count, last valid mask, configured sensor count, enabled state, initialized mask, and `SkippedTriggers`, without affecting acquisition.
- `CLEAR_EVENT_THROTTLE` re-arms `FrameIncomplete` and `FrameRecovered` at once, also while acquisition is enabled; it never takes the gate.

`acquireFrame` emits no events: it queues validity edges, and `schedIn` publishes them as `FrameIncomplete(sequence, validMask)` and `FrameRecovered(sequence)`, with the sequence number of the frame each edge occurred in.

`INITIALIZE_ALL`, `INITIALIZE_ALL_INTERNAL`, `SET_SENSOR_COUNT`, and the topology-time `configure(sensorCount)` are rejected while acquisition is enabled (`ReconfigurationRejected` event, `EXECUTION_ERROR`).

## Status values

`DriverStatus` distinguishes invalid arguments/configuration/state, SPI errors, identity mismatch, reset timeout, readback mismatch, FIFO misalignment/empty conditions, partial aggregate success, capacity overflow, `BUSY` (a control operation owns the manager), `STALE_DATA` (no newer sample), `IMPLAUSIBLE_DATA` (bus data impossible for an ADXL355), and `NVM_MISMATCH` (factory trim did not reload correctly). A frame is complete only when `validMask == (1 << sensorCount) - 1`.

The portable `SynchronizedArray` rejects devices that are not measuring with
`INVALID_STATE` without bus I/O, clears their valid bits, and preserves healthy
peers. It is a framework-independent alternative to the F Prime application
and has its own requirements.
