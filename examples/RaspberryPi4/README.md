# Raspberry Pi 4 topology fragments

These files provide the ADXL355-specific portion of a Linux F Prime deployment for one or two EVAL-ADXL355Z boards on Raspberry Pi SPI0.

They are integration fragments, not a standalone deployment. The enclosing topology must provide the standard F Prime command dispatcher, events, telemetry, text events, time, communications, and startup infrastructure.

## Files

- `instances.fpp` declares the application, two managers, and two Linux SPI drivers. The driver initialization phases open `/dev/spidev0.0` and `/dev/spidev0.1` in mode 0 at 10 MHz.
- `topology.fpp` connects application ports to managers and each manager to its device-agnostic SPI driver.

## Integration checklist

1. Copy or include the instances in the deployment namespace and replace the example base IDs using the deployment ID plan.
2. Merge the connection groups into the deployment topology.
3. Apply command, event, telemetry, text-event, and time connection patterns to the application and managers; apply driver event, telemetry, text-event, and time patterns as required by the F Prime version.
4. For a polling smoke test, use `INITIALIZE_ALL_INTERNAL` and manager `READ_SNAPSHOT` commands.
5. For synchronized acquisition, connect the real timing source to `adxl355App.acquireFrame` and a consumer to `adxl355App.frameReady`.

The complete wiring and command procedures are in the MkDocs page `docs/raspberry-pi-4.md`.
