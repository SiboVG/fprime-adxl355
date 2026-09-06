# Hardware integration

For one- or two-board transport bring-up with an EVAL-ADXL355Z, follow the [Raspberry Pi 4 bench example](raspberry-pi-4.md). Its internal-clock procedure is intentionally different from the synchronized flight profile below.

## CubeSpec CS EM ACC profile

The implementation uses the constraints in `CS EM ACC Design Notes.docx` for the seven-sensor R10 design.

| Signal or setting | Required value |
| --- | --- |
| Sensor count | 7 |
| ADXL355 ODR | 4 kHz |
| `INT2` | Shared 1.024 MHz external clock input |
| `DRDY/SYNC` | Shared 4 kHz synchronization input |
| Synchronization mode | External, no interpolation (`EXT_SYNC = 01`) |
| `SYNC` register | `0x05` (`EXT_CLK = 1`, `EXT_SYNC = 01`) |
| SPI mode | Mode 0 (`CPOL = 0`, `CPHA = 0`) |
| SPI clock | 5 to 10 MHz recommended; 10 MHz is the ADXL355 maximum |
| Chip select | One independently controlled CS per sensor |

With the external clock enabled, `INT2` cannot also carry an interrupt. The library rejects configurations that map an interrupt to `INT2` while `EXT_CLK` is set.

## Timing

At 1.024 MHz, four clock periods are 3.90625 microseconds. The synchronization pulse supplied to `DRDY/SYNC` must meet the data-sheet pulse-width and setup requirements; design for at least four external-clock cycles and maintain the required setup margin to the active clock edge.

Each frame reads 7 sensors x 10 bytes x 8 bits x 4,000 frames/s = 2.24 Mbit/s, excluding CS gaps and controller overhead. This fits at both 5 and 10 MHz, but the complete measured worst-case execution time must remain below the 250-microsecond frame period.

Use a logic analyzer during bring-up to verify:

- the 1.024 MHz clock is stable before enabling external-clock mode;
- all seven sensors receive the same synchronization edge;
- only one CS is low at a time;
- SCLK idles low and data is captured using SPI mode 0;
- each direct read contains one command byte and nine data bytes; and
- the seventh CS returns high before the next 4 kHz deadline.

The board design notes recommend gating the distributed SCLK so idle clock activity does not couple into unselected devices. Preserve that behavior in the PCU/FPGA implementation.

## Recommended startup order

1. Keep application acquisition disabled and hold back synchronization pulses.
2. Start and stabilize the 1.024 MHz external clock.
3. Configure all SPI endpoints for mode 0 and no more than 10 MHz.
4. Run `INITIALIZE_ALL`; require initialized mask `0x7F` for a nominal start.
5. Start the 4 kHz synchronization pulse train.
6. Enable frame acquisition.
7. Check the first frames for sequence continuity, `validMask == 0x7F`, sensible temperature, and no FIFO-overrun/status faults.

Configure all sensor registers before measurement. Do not use the default internal synchronization mode for this board if cross-sensor phase alignment is required.

## Direct registers versus FIFO

The synchronized application uses the contiguous `XDATA3` through `ZDATA1` registers. This is the lowest-overhead way to retrieve one time-aligned sample from each sensor after a known FPGA synchronization instant.

The core also supports FIFO reads and validates X-axis markers and empty markers. FIFO access is useful for lower-rate servicing or recovery, but it is not used in the default seven-sensor 4 kHz path. If FIFO is introduced into that path, preserve sample-to-timestamp correspondence explicitly and test overrun behavior at the full bus load.
