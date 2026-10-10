# ADXL355 contributor guidance

Before changing this library, read:

- `../../docs/engineering-practices/fprime.md` for the workspace-wide reusable flight-software practices distilled from the 2024 JPL workshop; and
- `docs/practices-audit.md` for the current library-specific conformance assessment, integration responsibilities, and open verification work.

Keep `docs/requirements-and-verification.md` synchronized with behavior and tests. Add or update requirement-to-test evidence whenever an interface, command, state transition, failure response, or timing assumption changes.

Preserve the application-manager-driver separation and the hardware-independent `Adxl355::Device` core. The acquisition path must remain fixed-capacity, bounded, and free of heap allocation. Treat concurrency, SPI completion status, timing margin, external synchronization, and calibration as explicit integration concerns rather than implicit assumptions.

Create the tool chain for the pinned F Prime revision (`35d4c0e`) with `../../tools/workspace/bootstrap-fprime-venv.sh` or the equivalent commands in `docs/testing.md`; do not pin a macOS SDK or rely on Rosetta. Before handing off a change, run the host tests, both generated F Prime component test executables, the static-analysis gate (`python3 scripts/static_analysis.py`, with the F Prime venv active and `FPRIME_FRAMEWORK_PATH` set; it must report zero findings), the documentation tests, and a strict MkDocs build; then refresh `docs/evidence/` and regenerate its manifest with `python3 tests/check_evidence.py --update`. Hardware timing or electrical claims require Raspberry Pi bench evidence or flight-target HIL evidence; do not infer them from emulator tests.

Fix static-analysis findings in the code. Suppress one only inline, on or just before the line, naming the check and the reason (`// NOLINTNEXTLINE(check-id): reason` or `// cppcheck-suppress checkId ; reason`); never add file-level, directory-level, or command-line suppressions for first-party code. A finding that reveals a real defect whose fix would change behavior is reported and fixed as a reviewed change, not suppressed. `scripts/static_analysis.py`, `scripts/install-cppcheck.sh`, `scripts/clang-tidy`, and `.clang-tidy` are byte-identical copies of the workspace reference in `tools/static-analysis/`; change them there, not here. Only `static-analysis.toml` is specific to this repository.

## Test-first, from the requirement

Develop test-first, and derive tests from what the code must do, not from how it does it:

1. Add or update the requirement (ID, `shall` statement, rationale or parent, method) in this repository's requirements matrix.
2. Design test cases from the requirement and the interface contract (datasheet, ICD, FPP model) without reading the implementation: nominal values computed independently, boundaries, invalid inputs, injected dependency faults, state transitions.
3. Watch the tests fail, implement the minimum to make them pass, then refactor.
4. When coverage shows unexecuted code, add a requirement-derived test, remove dead code, add the missing requirement, or justify the code by analysis. Never add a test only to execute a line.
5. Name the requirement ID in each test, and list the test in the requirement's row.

Coverage target (ECSS category C, CubeSpec): 100% statement coverage, tested or justified, and at least 80% decision coverage. The full procedure is in the CubeSpec workspace `AGENTS.md` (`../../AGENTS.md` in the workspace layout).
