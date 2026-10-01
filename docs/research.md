# Research and source comparison

## Primary sources

- [ADXL354/ADXL355 data sheet, Rev. D](https://www.analog.com/media/en/technical-documentation/data-sheets/adxl354_adxl355.pdf)
- [EVAL-ADXL354/EVAL-ADXL355 User Guide, UG-1030](https://www.analog.com/media/en/technical-documentation/user-guides/EVAL-ADXL354-355-UG-1030.pdf)
- [Raspberry Pi GPIO and SPI documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio-and-the-40-pin-header)
- [Analog Devices no-OS ADXL355 driver](https://github.com/analogdevicesinc/no-OS/tree/main/drivers/accel/adxl355)
- [Analog Devices no-OS implementation](https://github.com/analogdevicesinc/no-OS/blob/main/drivers/accel/adxl355/adxl355.c)
- [F Prime application-manager-driver pattern](https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/app-man-drv/)
- [F Prime device-driver development guide](https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-device-driver/)
- CubeSpec internal source: `Cubespec_Electronics/HW/02_EM_Boards/CS_EM_ACC/R10/DesignNotes/CS EM ACC Design Notes.docx`

## Analog Devices API mapping

The official no-OS implementation was used as an API and protocol cross-check, not copied into this library.

| Analog Devices no-OS API area | This library |
| --- | --- |
| SPI read/write helpers | `readRegister`, `writeRegister`; same `(address << 1) | read` command encoding |
| `adxl355_init`, `adxl355_soft_reset` | `initialize`, `softReset`, plus identity and configuration readback checks |
| operation mode | `startMeasurement`, `stopMeasurement` |
| range, ODR/LPF, HPF | `setRange`, `setFilter` |
| offsets | `setOffsets` |
| raw/scaled XYZ | `readRawSample`, `readSample` |
| raw/scaled temperature | `readTemperature` |
| status and FIFO entry/data APIs | `readStatus`, `readFifo`, `setFifoWatermark` |
| activity configuration | `configureActivity` |
| interrupt routing and polarity | `configureInterrupts` |
| self-test | `setSelfTest(enableMode, applyForce)` ST1/ST2 control, tracked and cleared by reset |

The no-OS header defines the `SYNC` register but does not expose a typed synchronization configuration API in its classic driver interface. This library adds `SyncMode`, external-clock conflict validation, and the explicit `externalSynchronized4kHz()` preset because synchronization is central to the CubeSpec hardware.

## Data-sheet decisions

- SPI uses mode 0 and is limited to 10 MHz.
- Direct acceleration results are signed 20-bit values left-justified in three bytes.
- A single 13-byte read begins at `STATUS` so X/Y/Z and `DATA_RDY` come from one transaction. The data sheet guarantees one data set only when the read starts after `DATA_RDY` and completes within about one output period.
- Temperature conversion uses nominal 25 degrees Celsius at code 1885 and nominal slope -9.05 LSB/degree Celsius.
- Typical sensitivities are used for engineering-unit conversion; production calibration is intentionally outside the device driver.
- External-clock mode reserves `INT2`, and external synchronization uses `DRDY/SYNC`.
- Reset completion is bounded by polling `STATUS.NVM_BUSY` rather than using an unbounded delay.
- `DATA_RDY` clears on a STATUS read and sets only for newer data, and STATUS bits 7:5 always read 0; the acquisition path uses both to reject stale and implausible bus data.
- A software reset can load NVM trim incorrectly on `REVID` <= 0x01; shadow registers 0x50 through 0x54 are compared before and after each reset, as the data sheet recommends.
- Temperature is not double buffered, so it is read until two consecutive reads agree.
- TEMP2 bits 7:4 are reserved and read 0 (0x07 at 25 °C on the TE0715 bench). A reading with them set, or outside the storage temperature range of −55 °C to +150 °C (Table 5), is not a temperature: a stuck-high MISO reads raw 4095 (about −219 °C), a stuck-low one raw 0 (about +233 °C).
- `EXT_SYNC = 01` without `EXT_CLK` is rejected (Table 14, not recommended); a missing external clock in the flight mode yields null outputs, reported as `STALE_DATA`. A clock that stops after running instead freezes the last sample with `DATA_RDY` set (TE0715 bench, 2026-10-02), reported as `FROZEN_DATA`.
- ST1 enables self-test mode and ST2 applies the force; ST2 without ST1 is rejected.
- Only `HPF_CORNER`, `INT_MAP`, ST1/ST2, and reset may change in measurement mode; every other control enters standby first.
- The FIFO is a stream-mode buffer that overruns continuously when not drained, so `FIFO_OVR` is not a fault in the direct-read path.

## EVAL-ADXL355Z bench decisions

- Both `VDD` and `VDDIO` are powered from 3.3 V; the exposed 1.8 V rails remain unconnected.
- SPI0 MOSI, MISO, and SCLK are shared, while CE0 and CE1 select the two evaluation boards independently.
- The evaluation-board user guide and schematic were cross-checked for P1/P2 numbering. The current sensor data sheet identifies device pin 7 as reserved, so the board's legacy `STBY` shunt is removed for the bench example.
- Internal-clock bring-up is kept distinct from external synchronization so a transport smoke test cannot be mistaken for a coherence test.
