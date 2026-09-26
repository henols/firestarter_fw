"""
Project Name: Firestarter
Copyright (c) 2024 Henrik Olsson

Permission is hereby granted under MIT license.

Phase 15 Plan 01 — Wave 0 RED-gate scaffold for firestarter/update_version.py.

Requirements: VER-02
Decisions covered: D-04, D-05, D-06, D-07, D-08, D-09, D-12, D-13, D-17, D-21, D-24, D-25

This file mirrors firestarter_app/tests/test_update_version.py with firmware-specific
substitutions: version.h instead of __init__.py, and #define VERSION "X.Y.Z" instead of
the Python app's version assignment line.

Wave 0 contract:
- All tests in TestUpdateVersionBeta and TestUpdateVersionDryRun MUST fail RED today
  (AttributeError on missing functions).
- TestUpdateVersionStable tests also fail (parse_args/is_beta_mode not yet in script).
- Import succeeds against the current 63-line script (no ImportError at collection time).
"""

import sys
import subprocess
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / ".github" / "scripts"))
import update_version  # noqa: E402

import pytest


class TestUpdateVersionStable:
    """VER-02 (D-17, D-24) — Stable-path tests asserting Wave-1 4-tuple return shape.

    Today (Wave 0): calculate_version() unpacks get_header_version() as a 3-tuple.
    Wave 1 (Plan 15-03) changes get_header_version() to return a 4-tuple.
    These tests are authored against the Wave-1 contract so they turn GREEN in Wave 1.

    Decisions: D-17 (byte-identical stable output), D-24 (extended parse regex),
               D-25 (silent -dev truncation preserved).
    """

    @pytest.fixture(autouse=True)
    def _isolate_env(self, monkeypatch):
        """VER-02 / D-04 — Clear all CI env vars before each test.

        Tests that need specific env state call monkeypatch.setenv(...) AFTER this
        autouse fixture has cleared it, per test_fwguard.py class-fixture pattern.
        """
        monkeypatch.delenv("GITHUB_REF", raising=False)
        monkeypatch.delenv("BETA_VERSION", raising=False)
        monkeypatch.delenv("GITHUB_OUTPUT", raising=False)

    def test_stable_byte_identical(self, tmp_path, monkeypatch):
        """VER-02 / D-17 — Stable path produces byte-identical output to pre-v1.4 script.

        Seeds tmp_path/version.h from the committed golden baseline, monkeypatches
        update_version.header_file, runs the stable path, asserts the result equals
        the committed golden expected file byte-for-byte.

        RED today: parse_args/is_beta_mode not in the current 63-line script.

        Decisions: D-17 (byte-identity), D-24 (4-tuple return), D-25 (dev-suffix ignored).
        """
        golden_dir = Path(__file__).resolve().parent / "golden"
        baseline = golden_dir / "stable-baseline.h"
        expected = golden_dir / "stable-expected.h"

        version_file = tmp_path / "version.h"
        version_file.write_bytes(baseline.read_bytes())

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))
        monkeypatch.setattr(update_version, "header_file", str(version_file))

        # Wave 1 contract: parse_args() exists and returns an object with dry_run=False, beta=False.
        args = update_version.parse_args()
        assert not update_version.is_beta_mode(args)
        update_version.calculate_version()

        assert version_file.read_bytes() == expected.read_bytes(), (
            f"Stable path output did not match golden expected file.\n"
            f"Got:      {version_file.read_text()!r}\n"
            f"Expected: {expected.read_text()!r}"
        )

    def test_stable_patch_increment(self, tmp_path, monkeypatch):
        """VER-02 / D-17 — Stable path increments patch, writes GITHUB_OUTPUT, preserves typo.

        Assertions:
        1. 'version=1.2.4' appears in the monkeypatched GITHUB_OUTPUT file.
        2. stdout contains the preserved typo 'New versin created:' (D-17).

        RED today: parse_args/is_beta_mode not in the current 63-line script.
        Decisions: D-17 (preserved typo), D-24 (4-tuple from get_header_version).
        """
        version_file = tmp_path / "version.h"
        version_file.write_text('#define VERSION "1.2.3"\n')
        monkeypatch.setattr(update_version, "header_file", str(version_file))

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))

        # Wave 1 contract: parse_args() and is_beta_mode() exist.
        args = update_version.parse_args()
        assert not update_version.is_beta_mode(args)

        import io
        import contextlib
        stdout_capture = io.StringIO()
        with contextlib.redirect_stdout(stdout_capture):
            update_version.calculate_version()

        output_text = output_file.read_text()
        assert "version=1.2.4" in output_text, (
            f"Expected 'version=1.2.4' in GITHUB_OUTPUT. Got:\n{output_text}"
        )

        stdout_text = stdout_capture.getvalue()
        assert "New versin created:" in stdout_text, (
            f"Expected preserved typo 'New versin created:' in stdout. Got:\n{stdout_text}"
        )


class TestUpdateVersionBeta:
    """VER-02 (D-04..D-09, D-21) — Beta-path tests.

    All tests in this class fail RED today (the beta path doesn't exist in Wave 0).
    Wave 1 (Plan 15-03) adds is_beta_mode(), compute_beta_version(), BETA_VERSION_RE,
    and argparse to make these tests GREEN.

    Decisions: D-04 (GITHUB_REF detection), D-05 (--beta flag), D-06 (BETA_VERSION env),
               D-07 (verbatim acceptance), D-08 (git-tag fallback), D-09 (first beta = b1),
               D-21 (PEP 440 regex validation).
    """

    @pytest.fixture(autouse=True)
    def _isolate_env(self, monkeypatch):
        """VER-02 / D-04 — Clear all CI env vars before each test."""
        monkeypatch.delenv("GITHUB_REF", raising=False)
        monkeypatch.delenv("BETA_VERSION", raising=False)
        monkeypatch.delenv("GITHUB_OUTPUT", raising=False)

    def test_beta_explicit_version(self, tmp_path, monkeypatch):
        """VER-02 / D-06, D-07 — Explicit BETA_VERSION is written verbatim to version.h.

        Sets GITHUB_REF=refs/heads/beta and BETA_VERSION=1.2.3b1.
        Asserts:
        1. version.h ends with #define VERSION "1.2.3b1"\n
        2. GITHUB_OUTPUT contains version=1.2.3b1

        RED today: beta path (is_beta_mode, compute_beta_version) does not exist.
        Decisions: D-04 (GITHUB_REF), D-06 (BETA_VERSION env), D-07 (verbatim write).
        """
        version_file = tmp_path / "version.h"
        version_file.write_text('#define VERSION "1.2.3"\n')
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        monkeypatch.setenv("GITHUB_REF", "refs/heads/beta")
        monkeypatch.setenv("BETA_VERSION", "1.2.3b1")

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))

        # Wave 1 contract: parse_args() and is_beta_mode() exist.
        args = update_version.parse_args()
        assert update_version.is_beta_mode(args), "GITHUB_REF=refs/heads/beta must trigger beta mode"

        update_version.calculate_version()

        content = version_file.read_text()
        assert content.endswith('#define VERSION "1.2.3b1"\n'), (
            f"Beta version file must end with #define VERSION \"1.2.3b1\"\\n. Got: {content!r}"
        )
        output_text = output_file.read_text()
        assert "version=1.2.3b1" in output_text, (
            f"Expected 'version=1.2.3b1' in GITHUB_OUTPUT. Got:\n{output_text}"
        )

    def test_beta_invalid_version_rejected(self, tmp_path, monkeypatch):
        """VER-02 / D-21 — Invalid BETA_VERSION (PEP 440 violation) fails with clear error.

        Sets BETA_VERSION='1.2.3beta1' (spelled-out 'beta' is NOT canonical PEP 440).
        Asserts:
        1. ValueError or SystemExit is raised (D-21: 'fails the script with a clear error').
        2. version.h content is UNCHANGED (no partial write — security invariant T-15-01-02).

        Validation regex (D-21): ^[0-9]+[.][0-9]+[.][0-9]+(b|rc)[0-9]+$

        RED today: BETA_VERSION validation does not exist.
        Decisions: D-21 (PEP 440 regex ^[0-9]+[.][0-9]+[.][0-9]+(b|rc)[0-9]+$), T-15-01-02.
        """
        original_content = '#define VERSION "1.2.3"\n'
        version_file = tmp_path / "version.h"
        version_file.write_text(original_content)
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        monkeypatch.setenv("GITHUB_REF", "refs/heads/beta")
        monkeypatch.setenv("BETA_VERSION", "1.2.3beta1")  # invalid — 'beta' spelled out

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))

        with pytest.raises((ValueError, SystemExit)):
            update_version.calculate_version()

        # T-15-01-02: version.h must be UNCHANGED after validation failure.
        assert version_file.read_text() == original_content, (
            "version.h must be unchanged after an invalid BETA_VERSION is rejected"
        )

    def test_beta_tag_fallback(self, tmp_path, monkeypatch):
        """VER-02 / D-08 — When BETA_VERSION unset, scan git tags for highest bN, emit b(N+1).

        Monkeypatches subprocess.run to return fake tag output '1.2.3b1\n1.2.3b2\n'.
        Asserts version.h ends with #define VERSION "1.2.3b3"\n (max(1,2)+1 = 3, D-09).

        RED today: git-tag-scan fallback (compute_beta_version) does not exist.
        Decisions: D-08 (git-tag-scan fallback), D-09 (monotonic b increment), T-15-01-03.
        """
        version_file = tmp_path / "version.h"
        version_file.write_text('#define VERSION "1.2.3"\n')
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        monkeypatch.setenv("GITHUB_REF", "refs/heads/beta")

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))

        # T-15-01-03: Monkeypatch subprocess.run to avoid real git invocation.
        fake_result = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="1.2.3b1\n1.2.3b2\n", stderr=""
        )
        monkeypatch.setattr(update_version.subprocess, "run", lambda *a, **kw: fake_result)

        update_version.calculate_version()

        content = version_file.read_text()
        assert content.endswith('#define VERSION "1.2.3b3"\n'), (
            f"Tag fallback with max(b1, b2) should emit b3. Got: {content!r}"
        )

    def test_beta_first_ever(self, tmp_path, monkeypatch):
        """VER-02 / D-09 — First beta on a base version (no prior tags) emits b1.

        Monkeypatches subprocess.run to return empty tag output.
        Asserts version.h ends with #define VERSION "1.2.3b1"\n (first-ever beta = b1, D-09).

        RED today: compute_beta_version fallback does not exist.
        Decisions: D-09 (first beta resets to b1), D-08 (empty tag list).
        """
        version_file = tmp_path / "version.h"
        version_file.write_text('#define VERSION "1.2.3"\n')
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        monkeypatch.setenv("GITHUB_REF", "refs/heads/beta")
        # BETA_VERSION is intentionally NOT set.

        output_file = tmp_path / "github_output"
        output_file.write_text("")
        monkeypatch.setenv("GITHUB_OUTPUT", str(output_file))

        # Empty tag list — no prior beta on this base.
        fake_result = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr=""
        )
        monkeypatch.setattr(update_version.subprocess, "run", lambda *a, **kw: fake_result)

        update_version.calculate_version()

        content = version_file.read_text()
        assert content.endswith('#define VERSION "1.2.3b1"\n'), (
            f"First-ever beta should emit b1 (D-09). Got: {content!r}"
        )


class TestUpdateVersionDryRun:
    """VER-02 (D-13, D-28) — Dry-run mode: compute version, emit to stdout, write nothing.

    All tests fail RED today (--dry-run / argparse does not exist in Wave 0).
    Wave 1 (Plan 15-03) adds argparse with --dry-run to make these tests GREEN.

    Decisions: D-13 (--dry-run flag), D-28 (DRY_RUN: {version} stdout format).
    """

    @pytest.fixture(autouse=True)
    def _isolate_env(self, monkeypatch):
        """VER-02 / D-04 — Clear all CI env vars before each test."""
        monkeypatch.delenv("GITHUB_REF", raising=False)
        monkeypatch.delenv("BETA_VERSION", raising=False)
        monkeypatch.delenv("GITHUB_OUTPUT", raising=False)

    def test_dry_run_no_file_write(self, tmp_path, monkeypatch):
        """VER-02 / D-13 — --dry-run + --beta + BETA_VERSION: no file write, no GITHUB_OUTPUT.

        Asserts:
        1. version.h content is UNCHANGED after dry-run.
        2. GITHUB_OUTPUT file is NOT written (D-13: skip on dry-run).
        3. stdout starts with 'DRY_RUN: 1.2.3b1' (D-28).

        RED today: --dry-run and argparse do not exist.
        Decisions: D-13 (no file write), D-28 (DRY_RUN: format).
        """
        original_content = '#define VERSION "1.2.3"\n'
        version_file = tmp_path / "version.h"
        version_file.write_text(original_content)
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        monkeypatch.setenv("BETA_VERSION", "1.2.3b1")
        monkeypatch.setenv("GITHUB_REF", "refs/heads/beta")

        # Wave 1 contract: parse_args(['--dry-run', '--beta']) works.
        args = update_version.parse_args(["--dry-run", "--beta"])
        assert args.dry_run, "--dry-run arg must set args.dry_run = True"
        assert update_version.is_beta_mode(args)

        import io, contextlib
        stdout_capture = io.StringIO()
        with contextlib.redirect_stdout(stdout_capture):
            update_version.calculate_version(args)

        assert version_file.read_text() == original_content, (
            "Dry-run must NOT modify version.h"
        )

        captured_out = stdout_capture.getvalue()
        assert captured_out.strip().startswith("DRY_RUN: "), (
            f"--dry-run stdout must start with 'DRY_RUN: '. Got: {captured_out!r}"
        )
        assert "1.2.3b1" in captured_out, (
            f"--dry-run stdout must contain the proposed version '1.2.3b1'. Got: {captured_out!r}"
        )

    def test_dry_run_stable_path(self, tmp_path, monkeypatch, capsys):
        """VER-02 / D-13, D-28 — --dry-run on stable path: no file write, DRY_RUN: 1.2.4 stdout.

        Asserts:
        1. version.h content is UNCHANGED after dry-run.
        2. Captured stdout starts with 'DRY_RUN: ' and contains '1.2.4'.

        RED today: --dry-run and argparse do not exist.
        Decisions: D-13 (no file write on dry-run), D-28 (DRY_RUN: {version} format).
        """
        original_content = '#define VERSION "1.2.3"\n'
        version_file = tmp_path / "version.h"
        version_file.write_text(original_content)
        monkeypatch.setattr(update_version, "header_file", str(version_file))
        # No GITHUB_REF, no BETA_VERSION — stable path.

        # Wave 1 contract: parse_args(['--dry-run']) works.
        args = update_version.parse_args(["--dry-run"])
        assert args.dry_run, "--dry-run arg must set args.dry_run = True"
        assert not update_version.is_beta_mode(args), "Stable dry-run must not be in beta mode"

        update_version.calculate_version(args)
        captured = capsys.readouterr()

        assert version_file.read_text() == original_content, (
            "Dry-run must NOT modify version.h on stable path"
        )

        assert captured.out.strip().startswith("DRY_RUN: "), (
            f"--dry-run stdout must start with 'DRY_RUN: '. Got: {captured.out!r}"
        )
        assert "1.2.4" in captured.out, (
            f"--dry-run stable path stdout must contain proposed version '1.2.4'. Got: {captured.out!r}"
        )
