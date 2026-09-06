# F Prime ADXL355 library

This library reads and controls Analog Devices ADXL355 accelerometers over SPI. It follows F Prime's application-manager-driver pattern and includes a seven-sensor acquisition application for the CubeSpec CS EM ACC board.

The supplied board preset configures every sensor for:

- 4 kHz output data rate;
- external synchronization without interpolation;
- a 1.024 MHz external clock on `INT2`;
- a synchronization pulse on `DRDY/SYNC`; and
- sequential 10-byte SPI reads that preserve a common sampling instant.

## Architecture

```text
FPGA sync/timestamp source
          |
          v
SynchronizedApplication
   | configure/read x 7
   v
Manager[0..6]              device-specific F Prime layer
   | Drv.SpiReadWrite x 7
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
| `Adxl355_Device` | Register protocol, configuration, conversions, FIFO, and controls |
| `Adxl355_Array` | Framework-independent seven-device frame aggregator |
| `Adxl355_Manager` | One device manager connected to a generic `Drv.SpiReadWrite` driver |
| `Adxl355_Application` | Initializes and sequentially reads up to seven managers per FPGA sync instant |

## Quick start

1. Add this directory to the F Prime project's `library_locations` and let its `library.cmake` register the modules.
2. Instantiate one `Adxl355.Manager` and one generic SPI driver per chip select, plus one `Adxl355.SynchronizedApplication`.
3. Open each SPI endpoint in mode 0 at 5 or 10 MHz.
4. Connect the FPGA sample-ready/timestamp output to `acquireFrame`.
5. Send `INITIALIZE_ALL(G2, HZ_4000, OFF)` to the application, confirm an initialized mask of `0x7F`, and then enable the synchronization pulses.
6. Consume `SampleFrame` from `frameReady`; `validMask` and `sensorStatus` identify partial frames without discarding healthy sensors.

For a one- or two-sensor Raspberry Pi 4 bench, use the internal-clock command `INITIALIZE_ALL_INTERNAL` and the wiring/topology example in [Raspberry Pi 4 bench example](docs/raspberry-pi-4.md). Internal mode verifies the complete SPI and F Prime stack but does not synchronize independent sensors.

The CubeSpec LED tutorial is registered through `sandbox/tutorials/led-blinker/settings.ini`. The library has been built and unit-tested with that project's F Prime 3.5.1 toolchain.

## Automated assurance

The repository workflow at `.github/workflows/adxl355.yml` builds the portable
host unit/integration tests, documentation contracts, and strict MkDocs site.
Independent jobs run address/undefined-behavior sanitizers, enforce an 80%
production line-coverage gate, and run `clang-tidy` with warnings treated as
errors. The generated F Prime component tests are a separate verification step
run through the registered CubeSpec sandbox; they have passed locally but are
not currently a job in this workflow.

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
