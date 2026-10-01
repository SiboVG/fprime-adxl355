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

With the external clock enabled, `INT2` cannot also carry an interrupt (data sheet Table 14 notes the mapping is ignored). The library rejects configurations that map an interrupt to `INT2` while `EXT_CLK` is set. It also rejects `EXT_SYNC = 01` without `EXT_CLK`, which Table 14 marks as not recommended because the digital filters reset on every synchronization pulse.

If the external clock is missing in this mode, the data sheet states that the device processes no data and the outputs read as null values. The library reports that as `STALE_DATA` for every sensor rather than as valid zero acceleration. If the clock stops after it has run, the device freezes with `DATA_RDY` set and keeps returning its last sample. The library reports that as `FROZEN_DATA` after eight repeated reads (ADXL-DEV-013). If only the synchronization pulses stop, the sensors keep running synchronized to the last pulse, so the FPGA timing path must monitor the pulse train.

## Timing

At 1.024 MHz, four clock periods are 3.90625 microseconds. The synchronization pulse supplied to `DRDY/SYNC` must meet the data-sheet pulse-width and setup requirements; design for at least four external-clock cycles and maintain the required setup margin to the active clock edge.

Each frame reads 7 sensors x 14 bytes x 8 bits x 4,000 frames/s = 3.136 Mbit/s, excluding CS gaps and controller overhead. Each read is one command byte plus the 13-byte STATUS..ZDATA1 burst; the four status bytes let the library detect stale and implausible data without a status-bearing SPI transport. Bus time is 78.4 microseconds per frame at 10 MHz and 156.8 microseconds at 5 MHz, so 10 MHz leaves substantially more margin. The complete measured worst-case execution time must remain below the 250-microsecond frame period.

Use a logic analyzer during bring-up to verify:

- the 1.024 MHz clock is stable before enabling external-clock mode;
- all seven sensors receive the same synchronization edge;
- only one CS is low at a time;
- SCLK idles low and data is captured using SPI mode 0;
- each acquisition read contains one command byte (0x09) and thirteen status/data bytes; and
- the seventh CS returns high before the next 4 kHz deadline.

The board design notes recommend gating the distributed SCLK so idle clock activity does not couple into unselected devices. Preserve that behavior in the PCU/FPGA implementation.

## Recommended startup order

1. Keep application acquisition disabled and hold back synchronization pulses.
2. Start and stabilize the 1.024 MHz external clock.
3. Configure all SPI endpoints for mode 0 and no more than 10 MHz.
4. Run `INITIALIZE_ALL`; require initialized mask `0x7F` for a nominal start.
5. Start the 4 kHz synchronization pulse train.
6. Wait at least one filter group delay (a few frame periods). The data sheet notes that one to three samples after the first synchronization pulse can be lost or corrupted.
7. Enable frame acquisition.
8. Check the first frames for sequence continuity, `validMask == 0x7F`, `SkippedTriggers == 0`, and sensible temperature. `FIFO_OVR` in `StatusRegister` is expected because the direct-read path does not drain the stream-mode FIFO.

## Software reset and NVM trim

On parts with `REVID` 0x01 or earlier, the data sheet warns that a software reset can load factory trim from NVM incorrectly, which corrupts the 0 g bias. Initialization therefore reads shadow registers 0x50 through 0x54 before the first software reset, compares them after every reset, and repeats the reset up to three times; persistent disagreement is reported as `NVM_MISMATCH`. The reference is only trustworthy if it is captured before any software reset since power-up, so power-cycle the sensors before the first initialization after a flight-software restart when the platform allows it.

Configure all sensor registers before measurement. Do not use the default internal synchronization mode for this board if cross-sensor phase alignment is required.

## Direct registers versus FIFO

The synchronized application uses the contiguous `STATUS` through `ZDATA1` registers. This is the lowest-overhead way to retrieve one time-aligned, freshness-checked sample from each sensor after a known FPGA synchronization instant.

The core also supports FIFO reads and validates X-axis markers and empty markers. FIFO access is useful for lower-rate servicing or recovery, but it is not used in the default seven-sensor 4 kHz path. If FIFO is introduced into that path, preserve sample-to-timestamp correspondence explicitly and test overrun behavior at the full bus load.
