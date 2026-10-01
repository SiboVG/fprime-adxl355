# ADXL355 practice audit

This audit compares the current library with the workspace-wide reusable workshop practices in `docs/engineering-practices/fprime.md`. It distinguishes properties established in source and tests from responsibilities that only a deployment or HIL campaign can close.

## Audit status

- **Conforms**: implemented with source or automated-test evidence.
- **Conforms with integration responsibility**: the library boundary is appropriate, but a deployment must provide and verify the stated behavior.
- **Intentional tailoring**: the code departs from a workshop suggestion for a documented, bounded reason.
- **Open verification**: a reasonable design or procedure exists, but the required analysis, coverage report, or hardware evidence is not yet present.

## Conformance matrix

| Practice | Status | Evidence and assessment |
| --- | --- | --- |
| Application-manager-driver layering | **Conforms** | `SynchronizedApplication` owns array-level mission behavior, `Manager` owns ADXL355 semantics and operator interfaces, and generic SPI drivers own bus access. Calls cross typed ports and flow down the layers. |
| Hardware and OS abstraction | **Conforms** | `Adxl355::Device` depends only on injected `SpiBus` and `Delay` interfaces; the F Prime OS delay lives in the separate `Adxl355_Platform` module. The core's only F Prime touchpoint is the compile-time `ADXL355_ASSERT` mapping. |
| FPP as the interface/topology model | **Conforms** | Types, ports, components, commands, events (with throttles), and telemetry are modeled in FPP; copyable topology fragments show one- and seven-sensor deployments. |
| Low coupling and high cohesion | **Conforms** | Register constants and protocol logic remain in the device target; aggregation does not know registers; the SPI endpoint does not know ADXL355 behavior. |
| Explicit execution context and ownership | **Conforms with integration responsibility** | The [architecture](architecture.md) lists every caller, its thread, and its worst-case hold or wait. A lock-free `AccessGate` in each component serializes the timing task and the command dispatcher: acquisition never waits (it returns `BUSY` or skips and counts the trigger), and every other caller waits at most 10 ms. Reconfiguration is rejected while acquisition is enabled. Buffers are caller-owned for one synchronous call and never retained. The deployment must drive `acquireFrame` from one timing task and budget the documented initialization blocking time. |
| Fixed allocation and bounded work | **Conforms** | The flight path uses fixed arrays and stack buffers, has no `new`/`delete`, performs at most seven manager calls, sizes SPI frames to the transfer, bounds FIFO work by capacity, caps reset polling at 255 attempts and NVM reload at three resets, and caps temperature rereads at three. The acquisition path emits no events or telemetry ("do not perform avoidable logging, telemetry, or blocking work" on timing-sensitive paths): fault edges go through a fixed 16-entry lock-free `ReportQueue` to a rate-group `schedIn` port, which publishes at most one queue's worth per call. |
| Checked values and deterministic state | **Conforms** | Fixed-width types, initialized members, fail-safe disabled acquisition, configuration validation (including the data sheet's not-recommended sync mode), identity checks, NVM shadow checks, write readback, and runtime-failure invalidation are exercised by nominal and fault tests. |
| Crisp lifecycle model | **Conforms** | The device holds one `LifecycleState` (`UNCONFIGURED`, `STANDBY`, `MEASURING`) with the transitions diagrammed in the architecture page; there are no independent lifecycle flags. Initialization is all or nothing. The application has two states (disabled, enabled) with reconfiguration allowed only when disabled. |
| Hardware data treated as untrusted | **Conforms** | Every acquisition read includes STATUS: `DATA_RDY` rejects stale data (including a stopped external clock, which produces null values), reserved STATUS bits reject implausible bus data, FIFO entry counts above 96 are rejected, and torn temperature reads are reread. HIL must confirm the reserved-bit behavior on silicon (ADXL-INT-008). |
| Off-nominal containment and operator evidence | **Conforms** | Per-sensor status, partial-frame masks, sequence gaps and `SkippedTriggers` for dropped triggers, error counters, bounded reset timeout, readback failure, edge-reported and throttled warnings, `ControlBusy`/`ApplicationBusy` events, and recovery events keep one sensor from hiding healthy data or flooding the event stream. Ground validation errors do not count as device faults. |
| Unambiguous status APIs | **Conforms** | Public device and FPP APIs use enums, and the manager uses F Prime's status-bearing SPI port, `Drv.SpiWriteRead`: a driver-reported failure, or a driver that changes a buffer, reaches the caller as `SPI_ERROR`. The portable core's `SpiBus::transfer` seam still returns a Boolean, which the manager derives from `Drv.SpiStatus` and the buffer checks. The freshness and plausibility checks cover faults a driver cannot see. |
| Assertions for internal impossibilities | **Conforms** | `ADXL355_ASSERT` (mapped to `FW_ASSERT` in F Prime builds) and `FW_ASSERT` guard unmapped statuses, out-of-range stored ranges, frame-path sensor counts, and proven transfer bounds. Input from the bus, ground, or configuration is always handled by status, never asserted. |
| Requirements-to-test traceability | **Conforms** | [Requirements and verification](requirements-and-verification.md) assigns stable IDs with rationale, maps each requirement to tests of the code it governs (the portable array has its own IDs), and lists open HIL requirements. |
| Interface-based unit and integration tests | **Conforms** | The host core uses a register emulator that models data-sheet writable masks, `DATA_RDY`/`FIFO_OVR` clearing, shadow-register reload, stuck MISO, and temperature tearing. The generated F Prime harnesses invoke only commands and ports, including re-entrant calls that exercise the gate deterministically, and assert on responses, events, telemetry, and output histories. |
| Coverage | **Conforms** | CI enforces line and branch gates on the portable core and a line gate on the F Prime components. Current measured values are in `docs/evidence/coverage.txt`. Coverage remains supporting evidence rather than a substitute for state/path review. |
| Static analysis and strict warnings | **Conforms** | `clang-tidy` (`clang-analyzer-*`, warnings as errors) runs on the core and the F Prime component sources; the local result is in `docs/evidence/static-analysis.txt`. Host builds use `-Wall -Wextra -Werror -pedantic`; F Prime builds of every authored source use `-Wall -Wextra -Werror`. The first remote workflow run to pass every job, including static analysis and the component tests, is [run 36331079661](https://github.com/SiboVG/fprime-adxl355/actions/runs/36331079661) on commit `354b404`. |
| Flight-like electrical and timing verification | **Open verification** | Raspberry Pi and flight-board procedures exist, but the repository contains no captured SPI/sync waveforms or worst-case execution-time result. Host tests cannot close these claims. |
| Release and operational evidence | **Conforms with integration responsibility** | Test, coverage, sanitizer, static-analysis, and documentation results are retained in `docs/evidence/`, and `tests/check_evidence.py` fails when a recorded source hash no longer matches. The enclosing CubeSpec release must archive dictionaries, topology/configuration, test as-runs, versions, known issues, and HIL results. |

## Integrator actions

Complete these before using the library in a flight deployment:

1. Allocate nonoverlapping component base IDs and connect the deployment's command, event, telemetry, text-event, and time patterns.
2. Open every SPI endpoint before manager initialization. Confirm mode 0, chip-select ownership, maximum clock, device permissions, and transfer-size support.
3. Invoke `acquireFrame` from one timing task, and connect the application's and every manager's `schedIn` to one rate group that runs below it. Budget command-dispatcher health pings for the initialization bound in the architecture page, or route initialization through an active component. Include the device, F Prime adapter, and SPI-driver stack depth in task sizing.
4. Hold acquisition disabled during startup. For external mode, hold the 1.024 MHz clock and the synchronization pulses low, set sensor count, initialize every required manager, require the expected initialized mask, start and stabilize the clock, start the synchronization pulses, wait at least one filter group delay, and then enable acquisition (ADXL-INT-003).
5. Measure worst-case seven-sensor execution time under representative load and keep verified margin below the 250 microsecond period. Linux userspace timing is a bench demonstration, not flight real-time evidence.
6. Use a platform SPI driver that reports failures through `Drv.SpiWriteRead` (`Drv.LinuxSpiDriver` and fprime-zephyr's `ZephyrSpiDriver` do), and test injected controller failures end to end.
7. Define health-monitor behavior for a stopped clock (`STALE_DATA` if it never ran, `FROZEN_DATA` if it stopped after running), a lost synchronization pulse train, persistent invalid masks, sequence gaps, `SkippedTriggers`, initialization failure, `NVM_MISMATCH`, and repeated reinitialization failure.
8. Power-cycle the sensors before the first initialization after a flight-software restart, or accept the NVM shadow-reference limitation (ADXL-INT-007).
9. Capture common clock, synchronization, CS, SCLK, MOSI, and MISO during HIL tests. Verify the timestamp passed to `acquireFrame` represents the electrical synchronization instant rather than SPI completion.
10. Keep calibration, axis alignment, temperature compensation, filtering for science use, and mission fault policy in the application layer with their own requirements and tests.
11. Rebuild generated code and rerun the full suite when changing F Prime versions; generated APIs and standard driver-port semantics are version-sensitive.

## Recommended documentation notes

The integration constraints above are also surfaced where an integrator will encounter them:

- [Architecture](architecture.md) explains execution context, state, memory, timing, freshness checks, and transport-status limitations.
- [Setup and examples](setup-and-examples.md) gives a controlled startup flow and deployment checklist.
- [Raspberry Pi 4 bench example](raspberry-pi-4.md) separates internal-clock smoke testing from external synchronization claims.
- [Testing](testing.md) separates host unit/integration evidence, F Prime harness tests, Raspberry Pi bench checks, and flight-target HIL closure.

This audit should be revisited after a behavior change, new execution context, new SPI driver, F Prime upgrade, or completed HIL campaign.
