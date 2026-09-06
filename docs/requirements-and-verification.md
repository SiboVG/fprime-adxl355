# Requirements and verification

This page provides stable library-level requirements and maps them to current evidence. It is a maintainable traceability baseline, not a substitute for the CubeSpec system requirements or the electrical ICD. “UT” means host or F Prime component unit test, “IT” means host integration test, “Inspection” means source/design review, and “HIL” means measurement on real hardware.

## Device-core requirements

| ID | Requirement | Verification and current evidence |
| --- | --- | --- |
| ADXL-DEV-001 | The device core shall identify an ADXL355 by reading and validating `DEVID_AD`, `DEVID_MST`, and `PARTID` before initialization succeeds. | UT: `IdentityReadUsesOneAutoIncrementingSpiTransaction`, `IdentityMismatchIsRejected` |
| ADXL-DEV-002 | The device core shall reject invalid range, ODR, HPF, synchronization, FIFO, pointer, size, and external-clock/INT2 configurations before unsafe bus access. | UT: `ExternalClockRejectsInt2InterruptMappings`, `SynchronizationControlRejectsClockAndInt2ConflictBeforeIo`, `ControlArgumentValidationDoesNotTouchTheBus` |
| ADXL-DEV-003 | Initialization shall reset the device, poll `NVM_BUSY` with a finite bound, program configuration in standby, verify writable registers, and enter measurement only after success. | UT: `SynchronizedPresetProgramsBoardModeBeforeMeasurement`, `ResetPollingHasABoundedNvmBusyTimeout`, `IgnoredMeasurementStartCannotInitializeDevice`, `FailedReconfigurationCannotResumeAPartiallyProgrammedDevice` |
| ADXL-DEV-004 | A direct acceleration read shall fetch X/Y/Z in one auto-incrementing register transaction, decode signed 20-bit values, and scale them for the configured range. | UT: `DirectReadDecodesSignedTwentyBitAxesAndScale`, `ScaleTracksConfiguredRange` |
| ADXL-DEV-005 | Temperature conversion shall follow the ADXL355 nominal intercept and slope. | UT: `TemperatureUsesDatasheetInterceptAndSlope` |
| ADXL-DEV-006 | Runtime controls that require standby shall stop measurement, write and verify the setting, resume only after success, and invalidate restart state if the update fails. | UT: `RuntimeRangeAndFilterChangesUseStandbyAndResumeMeasurement`, `FailedRuntimeReconfigurationInvalidatesDeviceState`, `IgnoredStandbyBlocksRuntimeConfiguration`, `OffsetsAndActivityConfigurationAreWrittenBigEndian`, `InterruptAndSelfTestControlPreserveUnrelatedBits` |
| ADXL-DEV-007 | FIFO reads shall be capacity-bounded and shall reject partial XYZ sets, empty markers, and invalid axis markers. | UT: `FifoReadReturnsWholeVerifiedXyzSets`, `FifoReadRejectsBrokenAxisMarkers`, `FifoReadRejectsAPartialXyzEntryCount` |
| ADXL-DEV-008 | The hardware-independent core shall report transport failure without advancing device state. | UT: `SpiFailureIsReportedWithoutChangingState`; Inspection: `SpiBus` abstraction |

## F Prime manager and application requirements

| ID | Requirement | Verification and current evidence |
| --- | --- | --- |
| ADXL-MGR-001 | Each manager shall own one device instance and expose configuration, acceleration, temperature, measurement, control, command, event, and telemetry interfaces through FPP. | F Prime UT: `ConfigureAndRead`, `ControlCommands`; Inspection: `Manager.fpp` |
| ADXL-MGR-002 | A manager shall reject sampling before successful initialization and shall translate core status to `DriverStatus` and command response. | F Prime UT: `ReadBeforeInitializationIsRejected`, `ConfigureAndRead` |
| ADXL-MGR-003 | Repeated equal failures on the high-rate read path shall increment the error count without emitting a warning event every frame, and recovery shall restore status. | F Prime UT: `HighRateFailuresAreEdgeReported` |
| ADXL-APP-001 | The synchronized application shall support a configured set of one through seven managers and reject any other count. | F Prime UT: `InitializeAllInternalMode`, `InvalidSensorCountsAreRejected`; host IT: `ArrayRejectsAnEighthSensor` |
| ADXL-APP-002 | On one acquisition trigger, the application shall read configured managers in deterministic index order and publish one frame containing the supplied synchronization timestamp and a monotonic sequence. | F Prime UT: `SevenSensorFrame`; host IT: `SevenSensorsInitializeAndProduceOneCoherentFrame` |
| ADXL-APP-003 | A sensor failure shall clear only that sensor's valid bit, preserve healthy samples and per-sensor status, report validity changes by edge, and report recovery. | F Prime UT: `InitializeAllReportsPartialMask`, `PartialFrameIsEdgeReportedAndRecovers`; host IT: `OneFailedSensorProducesPartialFrameWithoutHidingOtherSamples`, `ArrayRejectsInvalidDeviceStatesWithoutHidingHealthyPeer` |
| ADXL-APP-004 | Acquisition shall start disabled, and disabled acquisition shall perform no sensor reads and publish no frame. | F Prime UT: `AcquisitionStartsDisabled`, `DisabledAcquisitionDoesNoWork` |
| ADXL-ARCH-001 | The steady-state acquisition path shall use fixed-capacity storage, bounded loops, and no heap allocation. | Inspection: `SynchronizedApplication`, `SynchronizedArray`, and `Device`; compiler warnings treated as errors in host tests |
| ADXL-ARCH-002 | ADXL355 semantics shall remain independent of F Prime, the OS, and the concrete SPI implementation. | Inspection and host UT: `Adxl355::Device` depends only on `SpiBus` and `Delay` |

## Deployment and HIL requirements

These are integration requirements. They cannot be closed by the library's emulator tests.

| ID | Requirement | Verification status |
| --- | --- | --- |
| ADXL-INT-001 | The deployment shall open one mode-0 SPI endpoint per chip select at no more than 10 MHz before manager initialization. | Raspberry Pi bench or flight-target HIL; procedure documented, not executed in this repository |
| ADXL-INT-002 | The deployment shall serialize acquisition and manager commands, or otherwise prove thread-safe access to each manager and SPI endpoint. | Topology/execution-context analysis required per deployment |
| ADXL-INT-003 | The external-synchronization deployment shall stabilize the 1.024 MHz clock, initialize all required sensors, then enable common synchronization pulses and acquisition in a controlled order. | Flight-target HIL and signal capture required |
| ADXL-INT-004 | The full seven-sensor acquisition path shall complete with verified margin inside its 250 microsecond period under worst-case flight CPU and I/O load. | Flight-target timing measurement required |
| ADXL-INT-005 | The deployment shall detect and contain a failed or stalled sensor without suppressing healthy samples or flooding events. | F Prime integration test plus HIL fault injection required |
| ADXL-INT-006 | Science processing shall apply controlled per-sensor calibration, axis mapping, and temperature compensation above this transport library. | CubeSpec calibration and application-level verification required |

## Maintaining traceability

When behavior changes:

1. update the requirement or add a new stable ID;
2. update the design and API documentation;
3. add a test that fails before the implementation change;
4. place the exact test name or HIL artifact in the verification column; and
5. do not mark an electrical, timing, or flight-target requirement verified from host-only evidence.
