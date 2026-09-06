# ADXL355 contributor guidance

Before changing this library, read:

- `../../docs/engineering-practices/fprime.md` for the workspace-wide reusable flight-software practices distilled from the 2024 JPL workshop; and
- `docs/practices-audit.md` for the current library-specific conformance assessment, integration responsibilities, and open verification work.

Keep `docs/requirements-and-verification.md` synchronized with behavior and tests. Add or update requirement-to-test evidence whenever an interface, command, state transition, failure response, or timing assumption changes.

Preserve the application-manager-driver separation and the hardware-independent `Adxl355::Device` core. The acquisition path must remain fixed-capacity, bounded, and free of heap allocation. Treat concurrency, SPI completion status, timing margin, external synchronization, and calibration as explicit integration concerns rather than implicit assumptions.

Before handing off a change, run the host tests, both generated F Prime component test executables, the documentation tests, and a strict MkDocs build. Hardware timing or electrical claims require Raspberry Pi bench evidence or flight-target HIL evidence; do not infer them from emulator tests.
