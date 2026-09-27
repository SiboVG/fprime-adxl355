#!/usr/bin/env python3

import re
import sys
import unittest
from pathlib import Path


LIBRARY_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = LIBRARY_ROOT
WORKSPACE_ROOT = LIBRARY_ROOT.parents[1]
SHARED_PRACTICES = WORKSPACE_ROOT / "docs/engineering-practices/fprime.md"

sys.path.insert(0, str(LIBRARY_ROOT / "tests"))
import check_evidence  # noqa: E402


def mermaid_diagrams(relative_path: str) -> list[str]:
    page = (LIBRARY_ROOT / relative_path).read_text(encoding="utf-8")
    return re.findall(r"```mermaid\n(.*?)```", page, re.DOTALL)


def mermaid_label_lines(diagram: str) -> list[str]:
    labels = re.findall(
        r'\b[A-Za-z_][A-Za-z0-9_]*\[(?:"([^"]*)"|([^\]]*))\]', diagram
    )
    return [
        line.strip()
        for quoted, plain in labels
        for line in re.split(r"<br\s*/?>", quoted or plain)
    ]


class DocumentationTests(unittest.TestCase):
    def test_mkdocs_supports_light_dark_and_mermaid(self) -> None:
        mkdocs = (LIBRARY_ROOT / "mkdocs.yml").read_text(encoding="utf-8")
        requirements = (LIBRARY_ROOT / "requirements-docs.txt").read_text(encoding="utf-8")

        for required_text in (
            "name: material",
            'media: "(prefers-color-scheme: light)"',
            "scheme: default",
            "Switch to dark mode",
            'media: "(prefers-color-scheme: dark)"',
            "scheme: slate",
            "Switch to light mode",
            "pymdownx.superfences",
            "name: mermaid",
        ):
            with self.subTest(text=required_text):
                self.assertIn(required_text, mkdocs)
        self.assertIn("mkdocs-material", requirements)

    def test_architecture_timing_wiring_and_integration_have_visuals(self) -> None:
        expected_diagrams = {
            "docs/architecture.md": 2,
            "docs/raspberry-pi-4.md": 1,
            "docs/setup-and-examples.md": 1,
        }
        for relative_path, minimum_count in expected_diagrams.items():
            page = (LIBRARY_ROOT / relative_path).read_text(encoding="utf-8")
            with self.subTest(page=relative_path):
                self.assertGreaterEqual(page.count("```mermaid"), minimum_count)

    def test_mermaid_diagrams_defer_colors_to_the_active_theme(self) -> None:
        for relative_path in (
            "docs/architecture.md",
            "docs/raspberry-pi-4.md",
            "docs/setup-and-examples.md",
            "docs/testing.md",
        ):
            for diagram in mermaid_diagrams(relative_path):
                with self.subTest(page=relative_path):
                    self.assertNotRegex(
                        diagram,
                        r"(?i)\b(?:fill|stroke|color)\s*:",
                        "fixed Mermaid colors do not adapt to Material light/dark palettes",
                    )

    def test_flowchart_node_lines_are_short_enough_for_narrow_pages(self) -> None:
        for relative_path in (
            "docs/architecture.md",
            "docs/raspberry-pi-4.md",
            "docs/setup-and-examples.md",
            "docs/testing.md",
        ):
            for diagram in mermaid_diagrams(relative_path):
                for line in mermaid_label_lines(diagram):
                    with self.subTest(page=relative_path, label_line=line):
                        self.assertLessEqual(len(line), 22)

    def test_architecture_overview_avoids_subgraph_heading_collisions(self) -> None:
        architecture_diagrams = mermaid_diagrams("docs/architecture.md")
        self.assertGreaterEqual(len(architecture_diagrams), 3)
        overview = architecture_diagrams[0]
        self.assertNotIn(
            "subgraph ",
            overview,
            "arrows entering vertically centered subgraphs collide with their headings",
        )

    def test_sequence_labels_are_wrapped_for_document_width(self) -> None:
        architecture_diagrams = mermaid_diagrams("docs/architecture.md")
        self.assertGreaterEqual(len(architecture_diagrams), 2)
        acquisition = architecture_diagrams[1]

        participants = re.findall(
            r"^\s*participant\s+\S+\s+as\s+(.+)$", acquisition, re.MULTILINE
        )
        for participant in participants:
            for line in re.split(r"<br\s*/?>", participant):
                with self.subTest(participant_line=line):
                    self.assertLessEqual(len(line), 20)

        messages = re.findall(
            r"^\s*\S+\s*-+>>\s*\S+\s*:\s*(.+)$", acquisition, re.MULTILINE
        )
        for message in messages:
            for line in re.split(r"<br\s*/?>", message):
                with self.subTest(message_line=line):
                    self.assertLessEqual(len(line), 30)

        notes = re.findall(r"^\s*Note\s+[^:]+:\s*(.+)$", acquisition, re.MULTILINE)
        for note in notes:
            for line in re.split(r"<br\s*/?>", note):
                with self.subTest(note_line=line):
                    self.assertLessEqual(len(line), 24)

    def test_sequence_places_data_path_participants_next_to_each_other(self) -> None:
        acquisition = mermaid_diagrams("docs/architecture.md")[1]
        sensor_position = acquisition.index("participant Sensors")
        manager_position = acquisition.index("participant Managers")
        application_position = acquisition.index("participant App")

        self.assertLess(sensor_position, manager_position)
        self.assertLess(manager_position, application_position)
        self.assertNotIn(
            "\n    loop ",
            acquisition,
            "a three-participant loop caption is centered on the manager lifeline",
        )
        normalized_acquisition = re.sub(r"<br\s*/?>", " ", acquisition)
        self.assertIn("Repeat for each configured sensor", normalized_acquisition)
        self.assertNotRegex(
            acquisition,
            r"(?m)^\s*(\w+)->>\1:",
            "Mermaid centers self-message labels on the actor lifeline",
        )

    def test_state_transition_labels_are_concise(self) -> None:
        architecture_diagrams = mermaid_diagrams("docs/architecture.md")
        self.assertGreaterEqual(len(architecture_diagrams), 3)
        lifecycle = architecture_diagrams[2]
        transition_labels = re.findall(
            r"^\s*[^\n:]+:\s*(.+)$", lifecycle, re.MULTILINE
        )
        self.assertTrue(transition_labels)
        for label in transition_labels:
            for line in re.split(r"<br\s*/?>", label):
                with self.subTest(transition_line=line):
                    self.assertLessEqual(len(line), 10)

    def test_wiring_diagram_uses_shared_net_junctions(self) -> None:
        wiring_diagrams = mermaid_diagrams("docs/raspberry-pi-4.md")
        self.assertEqual(1, len(wiring_diagrams))
        wiring_diagram = wiring_diagrams[0]
        self.assertIn("PWR_NET", wiring_diagram)
        self.assertIn("SPI_NET", wiring_diagram)
        self.assertNotIn(
            "|shared ",
            wiring_diagram,
            "shared wiring belongs in one junction node instead of repeated edge labels",
        )

    def test_workshop_practices_are_reusable_and_audited(self) -> None:
        expected_files = (
            "AGENTS.md",
            "docs/practices-audit.md",
            "docs/requirements-and-verification.md",
        )
        missing = [relative_path for relative_path in expected_files if not (LIBRARY_ROOT / relative_path).is_file()]
        self.assertEqual([], missing, f"missing practice files: {missing}")

        mkdocs = (LIBRARY_ROOT / "mkdocs.yml").read_text(encoding="utf-8")
        for page in (
            "practices-audit.md",
            "requirements-and-verification.md",
        ):
            with self.subTest(page=page):
                self.assertIn(page, mkdocs)
        self.assertNotIn("jpl-fprime-practices.md", mkdocs)

        audit = (LIBRARY_ROOT / "docs/practices-audit.md").read_text(encoding="utf-8")
        self.assertIn("## Conformance matrix", audit)
        self.assertIn("## Integrator actions", audit)
        self.assertIn("status-bearing SPI", audit)

        agent_guidance = (LIBRARY_ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertIn("../../docs/engineering-practices/fprime.md", agent_guidance)
        self.assertIn("docs/practices-audit.md", agent_guidance)

    @unittest.skipUnless(
        SHARED_PRACTICES.is_file(),
        "workspace practice guide is only present in the FlightSoftware workspace layout",
    )
    def test_workspace_practice_guide_has_the_audited_sections(self) -> None:
        practices = SHARED_PRACTICES.read_text(encoding="utf-8")
        for heading in (
            "## Requirements and traceability",
            "## Architecture and interfaces",
            "## Deterministic design and implementation",
            "## Verification strategy",
            "## Reusable review checklist",
            "## Source material and scope",
        ):
            with self.subTest(heading=heading):
                self.assertIn(heading, practices)

    def test_raspberry_pi_example_is_complete_and_discoverable(self) -> None:
        expected_files = (
            "mkdocs.yml",
            "requirements-docs.txt",
            "docs/index.md",
            "docs/raspberry-pi-4.md",
            "examples/RaspberryPi4/README.md",
            "examples/RaspberryPi4/instances.fpp",
            "examples/RaspberryPi4/topology.fpp",
        )
        missing = [relative_path for relative_path in expected_files if not (LIBRARY_ROOT / relative_path).is_file()]
        self.assertEqual([], missing, f"missing documentation files: {missing}")

        mkdocs = (LIBRARY_ROOT / "mkdocs.yml").read_text(encoding="utf-8")
        self.assertIn("raspberry-pi-4.md", mkdocs)

        guide = (LIBRARY_ROOT / "docs/raspberry-pi-4.md").read_text(encoding="utf-8")
        for required_text in (
            "EVAL-ADXL355Z",
            "P1 pin 1",
            "P2 pin 6",
            "physical pin 19",
            "/dev/spidev0.0",
            "/dev/spidev0.1",
            "INITIALIZE_ALL_INTERNAL",
            "Never connect Raspberry Pi 5 V",
        ):
            with self.subTest(text=required_text):
                self.assertIn(required_text, guide)

        instances = (LIBRARY_ROOT / "examples/RaspberryPi4/instances.fpp").read_text(encoding="utf-8")
        self.assertIn("open(0, 0, Drv::SPI_FREQUENCY_10MHZ", instances)
        self.assertIn("open(0, 1, Drv::SPI_FREQUENCY_10MHZ", instances)

        topology = (LIBRARY_ROOT / "examples/RaspberryPi4/topology.fpp").read_text(encoding="utf-8")
        self.assertIn("adxl355App.configureSensor[0] -> adxl355Manager0.configure", topology)
        self.assertIn("adxl355Manager1.spi -> adxl355Spi1.SpiWriteRead", topology)

    def test_ci_enforces_coverage_static_analysis_and_strict_docs(self) -> None:
        workflow_path = REPOSITORY_ROOT / ".github/workflows/adxl355.yml"
        self.assertTrue(workflow_path.is_file())
        workflow = workflow_path.read_text(encoding="utf-8")
        for required_text in (
            "adxl355-host-tests",
            "ADXL355_ENABLE_COVERAGE=ON",
            "--fail-under-line",
            "--fail-under-branch",
            "clang-tidy",
            "--warnings-as-errors=*",
            "Adxl355/Manager/Manager.cpp",
            "Adxl355/Application/SynchronizedApplication.cpp",
            "mkdocs build --strict",
            "check_evidence.py",
        ):
            with self.subTest(text=required_text):
                self.assertIn(required_text, workflow)

        host_cmake = (LIBRARY_ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("option(ADXL355_ENABLE_COVERAGE", host_cmake)
        self.assertTrue((LIBRARY_ROOT / ".clang-tidy").is_file())

    def test_generated_component_tests_run_in_ci_with_a_coverage_gate(self) -> None:
        workflow = (REPOSITORY_ROOT / ".github/workflows/adxl355.yml").read_text(encoding="utf-8")
        readme = " ".join((LIBRARY_ROOT / "README.md").read_text(encoding="utf-8").split())
        testing = " ".join((LIBRARY_ROOT / "docs/testing.md").read_text(encoding="utf-8").split())

        for executable in ("Adxl355_Manager_ut_exe", "Adxl355_Application_ut_exe"):
            with self.subTest(executable=executable):
                self.assertIn(executable, workflow)
        self.assertIn("Adxl355/(Manager|Application|Platform)/", workflow)
        self.assertNotIn("separate verification step", readme)
        self.assertNotIn("not yet part of", testing)

    def test_practice_audit_points_to_retained_evidence(self) -> None:
        audit = (LIBRARY_ROOT / "docs/practices-audit.md").read_text(encoding="utf-8")
        architecture = (LIBRARY_ROOT / "docs/architecture.md").read_text(encoding="utf-8")

        self.assertIn("clang-tidy", audit)
        self.assertIn("docs/evidence/", audit)
        self.assertIn("docs/evidence/", architecture)
        self.assertNotIn("Boolean invariants", architecture)

    def test_testing_guide_uses_the_actual_static_analysis_tool(self) -> None:
        testing = (LIBRARY_ROOT / "docs/testing.md").read_text(encoding="utf-8")

        self.assertIn("clang-tidy", testing)
        self.assertNotIn("clang-analyzer checks, warnings", testing)

    def test_measured_coverage_lives_in_evidence_not_prose(self) -> None:
        pinned = re.compile(r"\b\d+ of \d+ (?:production )?lines\b|\b\d+\.\d+%")
        for relative_path in (
            "README.md",
            "docs/index.md",
            "docs/testing.md",
            "docs/practices-audit.md",
            "docs/architecture.md",
        ):
            text = (LIBRARY_ROOT / relative_path).read_text(encoding="utf-8")
            with self.subTest(page=relative_path):
                self.assertIsNone(pinned.search(text), "measured numbers belong in docs/evidence")
        for evidence in (
            "coverage.txt",
            "host-tests.txt",
            "component-tests.txt",
            "sanitizers.txt",
            "static-analysis.txt",
            "source-sha256.json",
        ):
            with self.subTest(evidence=evidence):
                self.assertTrue((LIBRARY_ROOT / "docs/evidence" / evidence).is_file())

    def test_evidence_manifest_matches_the_sources(self) -> None:
        self.assertEqual([], check_evidence.problems())

    def test_readme_and_testing_summarize_current_assurance_state(self) -> None:
        readme = " ".join((LIBRARY_ROOT / "README.md").read_text(encoding="utf-8").split())
        testing = " ".join((LIBRARY_ROOT / "docs/testing.md").read_text(encoding="utf-8").split())

        for token in (
            ".github/workflows/adxl355.yml",
            "sanitizers",
            "clang-tidy",
            "docs/evidence/",
            "Pending hardware execution",
        ):
            with self.subTest(readme_token=token):
                self.assertIn(token, readme)

        for token in (
            "check_evidence.py",
            "Physical Raspberry Pi and flight-target HIL remain open",
        ):
            with self.subTest(testing_token=token):
                self.assertIn(token, testing)

    def test_timing_documents_match_the_status_checked_burst(self) -> None:
        hardware = " ".join((LIBRARY_ROOT / "docs/hardware-integration.md").read_text(encoding="utf-8").split())
        architecture = " ".join((LIBRARY_ROOT / "docs/architecture.md").read_text(encoding="utf-8").split())

        self.assertIn("7 sensors x 14 bytes", hardware)
        self.assertNotIn("10 bytes x 8 bits", hardware)
        self.assertIn("78.4 microseconds", architecture)
        self.assertIn("STALE_DATA", architecture)


if __name__ == "__main__":
    unittest.main()
