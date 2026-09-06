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
| Hardware and OS abstraction | **Conforms** | `Adxl355::Device` depends only on injected `SpiBus` and `Delay` interfaces. The same core is compiled against host emulators and the F Prime manager adapter. |
| FPP as the interface/topology model | **Conforms** | Types, ports, components, commands, events, and telemetry are modeled in FPP; copyable topology fragments show one- and seven-sensor deployments. |
| Low coupling and high cohesion | **Conforms** | Register constants and protocol logic remain in the device target; aggregation does not know registers; the SPI endpoint does not know ADXL355 behavior. |
| Explicit execution context and ownership | **Conforms with integration responsibility** | Both components are passive and execute on the invoking thread. SPI and frame buffers are caller-owned for a synchronous call and are not retained. The deployment must serialize commands with acquisition. |
| Fixed allocation and bounded work | **Conforms** | The flight path uses fixed arrays and stack buffers, has no `new`/`delete`, performs at most seven manager calls, bounds FIFO work by capacity, and caps reset polling at 255 attempts. Dynamic STL storage is confined to host test code. |
| Checked values and deterministic state | **Conforms** | Fixed-width types, initialized members, fail-safe disabled acquisition, configuration validation, identity checks, write readback, runtime-failure invalidation, and explicit configured/initialized/measuring invariants are exercised by nominal and fault tests. |
| Crisp lifecycle model | **Intentional tailoring** | The device lifecycle is small and encoded by documented invariants rather than an FPP state machine. A state diagram is maintained in the architecture page; any added mode complexity should trigger conversion to a formal state machine. |
| Off-nominal containment and operator evidence | **Conforms** | Per-operation status, partial-frame masks, error counters, bounded reset timeout, readback failure, edge-limited warnings, and recovery events prevent one sensor from hiding healthy data or flooding the event stream. |
| Unambiguous status APIs | **Intentional tailoring / limitation** | Public device and FPP APIs use enums. The smallest `SpiBus::transfer` seam returns Boolean success, and F Prime 3.5.1 `Drv.SpiReadWrite` cannot propagate a Linux transfer result. A flight deployment needing end-to-end causality should use a **status-bearing SPI** port or adapter. |
| Requirements-to-test traceability | **Conforms** | [Requirements and verification](requirements-and-verification.md) assigns stable IDs and maps current unit/integration tests and open HIL requirements. Keep it synchronized with every behavioral change. |
| Interface-based unit and integration tests | **Conforms** | The host core uses an SPI register emulator; generated F Prime harnesses invoke commands and ports and assert on responses, events, telemetry, and output histories; the seven-device integration test injects a single-sensor fault. |
| Coverage | **Conforms** | CI compiles host code with `-Wall -Wextra -Werror -pedantic` and enforces at least 80% line coverage over the hardware-independent production core. The current host report covers 473 of 548 production lines (86.3%). Coverage remains supporting evidence rather than a substitute for state/path review. |
| Static analysis | **Open verification** | CI is configured to run `clang-tidy` with warnings treated as errors. The tool was unavailable on the review host, so retain a successful CI report before closing this evidence item. |
| Flight-like electrical and timing verification | **Open verification** | Raspberry Pi and flight-board procedures exist, but the repository contains no captured SPI/sync waveforms or worst-case execution-time result. Host tests cannot close these claims. |
| Release and operational evidence | **Conforms with integration responsibility** | Setup, wiring, API, limitations, and test procedures are documented. The enclosing CubeSpec release must archive dictionaries, topology/configuration, test as-runs, versions, known issues, and HIL results. |

## Integrator actions

Complete these before using the library in a flight deployment:

1. Allocate nonoverlapping component base IDs and connect the deployment's command, event, telemetry, text-event, and time patterns.
2. Open every SPI endpoint before manager initialization. Confirm mode 0, chip-select ownership, maximum clock, device permissions, and transfer-size support.
3. Run manager access from one execution context or serialize commands and acquisition. Include the device, F Prime adapter, and SPI-driver stack depth in task sizing.
4. Hold acquisition disabled during startup. For external mode, stabilize the 1.024 MHz clock, set sensor count, initialize every required manager, require the expected initialized mask, then enable synchronization pulses and acquisition.
5. Measure worst-case seven-sensor execution time under representative load and keep verified margin below the 250 microsecond period. Linux userspace timing is a bench demonstration, not flight real-time evidence.
6. Decide whether the standard F Prime 3.5.1 SPI port's lack of return status is acceptable. If not, standardize a status-bearing SPI boundary across the deployment and test injected controller failures end to end.
7. Define health-monitor behavior for a stopped clock, persistent invalid mask, sequence gaps, FIFO overrun, initialization failure, and repeated reinitialization failure.
8. Capture common clock, synchronization, CS, SCLK, MOSI, and MISO during HIL tests. Verify the timestamp passed to `acquireFrame` represents the electrical synchronization instant rather than SPI completion.
9. Keep calibration, axis alignment, temperature compensation, filtering for science use, and mission fault policy in the application layer with their own requirements and tests.
10. Rebuild generated code and rerun the full suite when changing F Prime versions; generated APIs and standard driver-port semantics are version-sensitive.

## Recommended documentation notes

The integration constraints above are also surfaced where an integrator will encounter them:

- [Architecture](architecture.md) explains execution context, state, memory, timing, and transport-status limitations.
- [Setup and examples](setup-and-examples.md) gives a controlled startup flow and deployment checklist.
- [Raspberry Pi 4 bench example](raspberry-pi-4.md) separates internal-clock smoke testing from external synchronization claims.
- [Testing](testing.md) separates host unit/integration evidence, F Prime harness tests, Raspberry Pi bench checks, and flight-target HIL closure.

This audit should be revisited after a behavior change, new execution context, new SPI driver, F Prime upgrade, or completed HIL campaign.
