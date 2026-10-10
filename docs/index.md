# F Prime ADXL355 library

The site follows the operating-system color preference on first load and provides a header toggle for light and dark modes. Diagrams are rendered from version-controlled Mermaid source and adapt with the documentation theme.

This library reads and controls Analog Devices ADXL355 accelerometers over SPI. It separates synchronized acquisition policy, device management, and platform SPI access according to F Prime's application-manager-driver pattern.

Use the documentation in this order:

1. Read the [architecture](architecture.md) to understand component responsibilities and timing assumptions.
2. Follow the [Raspberry Pi 4 bench example](raspberry-pi-4.md) for one or two EVAL-ADXL355Z boards.
3. Use [hardware integration](hardware-integration.md) when moving to the seven-sensor CubeSpec hardware.
4. Refer to the [API](api.md), [setup](setup-and-examples.md), and [testing](testing.md) pages while integrating a deployment.
5. Use the [requirements matrix](requirements-and-verification.md), workspace-wide `docs/engineering-practices/fprime.md` guide, and [library audit](practices-audit.md) during review and release closeout.

The Raspberry Pi example is a functional transport and sensor bring-up path. Linux userspace is not the timing authority for the final 4 kHz synchronized flight acquisition path.

The automated workflow separately gates host tests, sanitizers, core and
component coverage, the generated F Prime component tests, static analysis
(cppcheck and `clang-tidy`, zero findings), documentation contracts, the evidence manifest, and a strict site render. Local
results are retained in `docs/evidence/`. Physical Raspberry Pi execution,
synchronized multi-sensor waveforms, flight-target timing, and fault injection
remain HIL work rather than automated evidence.

## Build these docs

From `libraries/fprime-adxl355`:

```sh
python3 -m venv .venv-docs
. .venv-docs/bin/activate
python -m pip install -r requirements-docs.txt
mkdocs build --strict
```

The rendered site is written to `site/`. For live editing, run `mkdocs serve` from the same directory.
