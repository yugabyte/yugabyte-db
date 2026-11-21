# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

"""Tests for build-support/lint.py's script-and-regex runner: a linter script that exits non-zero
fails the file, with or without findings."""

import importlib.util
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


lint = _load("yb_lint_script_and_regex", REPO_ROOT / "build-support" / "lint.py")

# the regex of the postgres linter in .arclint
FINDING_REGEX = ("/^(?P<severity>\\w+):(?P<name>\\w+):(?P<message>[^:]+):(?P<line>\\d+):"
                 "(?P<original>.*)/m")


def _run(tmp_path, body):
    script = tmp_path / "stub.sh"
    script.write_text("#!/usr/bin/env bash\n" + body + "\n")
    script.chmod(0o755)
    config = {"script-and-regex.script": str(script), "script-and-regex.regex": FINDING_REGEX}
    linter = lint.Linter.build("stub", config, [])
    return lint.run_script_and_regex(linter, ["some/file.c"])


def _codes(msgs):
    return [m.code for m in msgs]


def test_findings_with_exit_0_are_reported_as_before(tmp_path):
    msgs = _run(tmp_path, "echo 'warning:bad_style:a message:12:x = 1;'\nexit 0")
    assert _codes(msgs) == ["bad_style"]
    assert msgs[0].line == 12


def test_a_clean_file_reports_nothing(tmp_path):
    assert _run(tmp_path, "exit 0") == []


def test_a_failure_without_findings_fails_the_file(tmp_path):
    # check_ctags in src/lint/common.sh, then `check_ctags || exit 1`
    msgs = _run(tmp_path, "echo 'Please install Exuberant Ctags' >&2\n"
                          "echo 'HINT: apt install exuberant-ctags' >&2\nexit 1")
    assert _codes(msgs) == ["script-failed"]
    assert msgs[0].severity == "error"
    assert msgs[0].message == ("linter script exited with status 1: "
                               "Please install Exuberant Ctags")


def test_a_failure_after_findings_fails_the_file_too(tmp_path):
    # postgres.sh prints the early rules' findings, then stops at `check_ctags || exit 1`; the
    # rules after that did not run, so the findings alone must not decide the file.
    msgs = _run(tmp_path, "echo 'warning:missing_yb_marker_for_yb_changes:a message:3:x'\n"
                          "echo 'Please install Exuberant Ctags' >&2\nexit 1")
    assert sorted(_codes(msgs)) == ["missing_yb_marker_for_yb_changes", "script-failed"]
    assert msgs[0].message.endswith(": Please install Exuberant Ctags")


def test_the_reason_is_the_last_stdout_line_that_is_not_a_finding(tmp_path):
    # postgres.sh echoes a failed fetch's error on stdout and the traceback goes to stderr
    msgs = _run(tmp_path, "echo 'warning:a_rule:a message:3:x'\n"
                          "echo 'URLError: <urlopen error timed out>'\n"
                          "echo 'Traceback (most recent call last):' >&2\nexit 1")
    failed = [m for m in msgs if m.code == "script-failed"]
    assert failed[0].message == ("linter script exited with status 1: "
                                 "URLError: <urlopen error timed out>")


def test_a_failure_with_no_output_gives_the_status(tmp_path):
    msgs = _run(tmp_path, "exit 3")
    assert [m.message for m in msgs] == ["linter script exited with status 3"]
