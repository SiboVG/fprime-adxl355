# Seven-sensor topology template

`instances.fpp` and `topology.fpp` provide the complete ADXL355-specific instance and connection fan-out for seven sensors. They are templates rather than a standalone deployment because command, event, telemetry, time, FPGA timing, and frame-consumer components belong to the enclosing CubeSpec topology.

## Integration

1. Copy the instances into the deployment namespace and replace the example base IDs using the deployment's ID allocation plan.
2. Merge the three connection groups into the deployment topology.
3. Apply the normal command/event/telemetry/time connection patterns to `adxl355App` and all seven managers. Apply driver event/telemetry/time connections as required by the chosen platform driver.
4. Connect the FPGA sample-ready output to `adxl355App.acquireFrame` and connect `frameReady` to the mission data path.
5. Open each SPI endpoint before `INITIALIZE_ALL`.

For Linux bench bring-up, the equivalent initialization is:

```cpp
bool ok = true;
ok = CubeSpec::adxl355Spi0.open(0, 0, Drv::SPI_FREQUENCY_10MHZ,
                                Drv::SPI_MODE_CPOL_LOW_CPHA_LOW) && ok;
ok = CubeSpec::adxl355Spi1.open(0, 1, Drv::SPI_FREQUENCY_10MHZ,
                                Drv::SPI_MODE_CPOL_LOW_CPHA_LOW) && ok;
// Repeat chip selects 2 through 6.
```

The Linux device tree must expose `/dev/spidev0.0` through `/dev/spidev0.6`; otherwise use the bus/select numbers actually assigned by the platform. The flight implementation should substitute the Zynq/FPGA SPI component if Linux userspace cannot meet the measured 4 kHz deadline.

The example intentionally leaves initialization out of FPP phase annotations so a failed open can be handled using the deployment's existing startup policy.

