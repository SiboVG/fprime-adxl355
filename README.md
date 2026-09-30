# F Prime ADXL355 library

This library reads and controls Analog Devices ADXL355 accelerometers over SPI. It follows F Prime's application-manager-driver pattern and includes a seven-sensor acquisition application for the CubeSpec CS EM ACC board.

The supplied board preset configures every sensor for:

- 4 kHz output data rate;
- external synchronization without interpolation;
- a 1.024 MHz external clock on `INT2`;
- a synchronization pulse on `DRDY/SYNC`; and
- sequential 14-byte, status-checked SPI reads that preserve a common sampling instant and reject stale or implausible data.

## Architecture

```text
FPGA sync/timestamp source
          |
          v
SynchronizedApplication
   | configure/read x 7
   v
Manager[0..6]              device-specific F Prime layer
   | Drv.SpiWriteRead x 7
   v
platform SPI driver[0..6]  device-agnostic hardware layer
   | shared bus, separate chip selects
   v
ADXL355[0..6]
```

The register-protocol core has no dependency on F Prime or a particular operating system. SPI and delay behavior are injected through `SpiBus` and `Delay`, so the same code is used by the flight components and host-side emulator tests.

## Modules

| F Prime target | Purpose |
| --- | --- |
| `Adxl355_Types` | FPP enums, structs, arrays, and typed ports |
| `Adxl355_Device` | Register protocol, configuration, conversions, FIFO, controls, and the non-blocking `AccessGate` |
| `Adxl355_Platform` | F Prime OS delay adapter for the portable `Delay` interface |
| `Adxl355_Array` | Framework-independent seven-device frame aggregator |
| `Adxl355_Manager` | One device manager connected to a generic `Drv.SpiWriteRead` driver |
| `Adxl355_Application` | Initializes and sequentially reads up to seven managers per FPGA sync instant |

## Quick start

1. Add this directory to the F Prime project's `library_locations` and let its `library.cmake` register the modules.
2. Instantiate one `Adxl355.Manager` and one generic SPI driver per chip select, plus one `Adxl355.SynchronizedApplication`.
3. Open each SPI endpoint in mode 0 at 5 or 10 MHz.
4. Connect the FPGA sample-ready/timestamp output to `acquireFrame`, and a rate group below it to `schedIn` of the application and of every manager (fault events).
5. Send `INITIALIZE_ALL(G2, HZ_4000, OFF)` to the application, confirm an initialized mask of `0x7F`, and then enable the synchronization pulses.
6. Consume `SampleFrame` from `frameReady`; `validMask` and `sensorStatus` identify partial frames without discarding healthy sensors.

For a one- or two-sensor Raspberry Pi 4 bench, use the internal-clock command `INITIALIZE_ALL_INTERNAL` and the wiring/topology example in [Raspberry Pi 4 bench example](docs/raspberry-pi-4.md). Internal mode verifies the complete SPI and F Prime stack but does not synchronize independent sensors.

The library is built and tested against F Prime `35d4c0e` (v4.2.0-267), the revision fprime-csp-libparam and the TE0715 Zephyr workspace use.

## Automated assurance

The repository workflow at `.github/workflows/adxl355.yml` builds the portable
host unit/integration tests, documentation contracts, the evidence-manifest
check, and a strict MkDocs site. Independent jobs run address/undefined-behavior
sanitizers, enforce line and branch coverage gates on the portable core, and run
the generated F Prime component tests against that F Prime revision with a component
line-coverage gate. `clang-tidy` runs with warnings treated as errors on both the
core and the component sources. Executed results are retained in `docs/evidence/`
and pinned to the exact sources. Before the move to F Prime `35d4c0e`, the first remote workflow run to pass every job was
[run 36331079661](https://github.com/SiboVG/fprime-adxl355/actions/runs/36331079661) on commit `354b404`.

Pending hardware execution includes the physical Raspberry Pi/EVAL-ADXL355Z
bring-up, external-clock and synchronization captures, seven-sensor
worst-case timing, and flight-target fault injection. Automated host evidence
must not be used to close those requirements.

## Documentation

- [Architecture and design](docs/architecture.md)
- [Hardware integration](docs/hardware-integration.md)
- [Raspberry Pi 4 and EVAL-ADXL355Z bench example](docs/raspberry-pi-4.md)
- [API reference](docs/api.md)
- [Setup and examples](docs/setup-and-examples.md)
- [Testing](docs/testing.md)
- [Requirements and verification](docs/requirements-and-verification.md)
- Workspace-wide reusable practices: `docs/engineering-practices/fprime.md`
- [ADXL355 practice audit](docs/practices-audit.md)
- [Research and source comparison](docs/research.md)
- [Seven-sensor topology template](examples/SevenSensorTopology/README.md)
- [Raspberry Pi 4 topology template](examples/RaspberryPi4/README.md)

Render the complete documentation site from this directory with:

```sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r requirements-docs.txt
mkdocs build --strict
```
