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

"""Tests for build-support/lint.py's main(): a linter runner that raises is reported as an error,
so lint.py exits 1 instead of passing the files that linter never checked."""

import importlib.util
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


lint = _load("yb_lint_runner_exceptions", REPO_ROOT / "build-support" / "lint.py")


def _raising(linter, files):
    raise ValueError("unknown extension ?<l at position 4")


def _clean(linter, files):
    return []


def _one_warning(linter, files):
    return [lint.Message(files[0], 3, "warning", "style", "a warning", linter.name)]


def _main(monkeypatch, runners, files):
    config = {"linters": {name: {"type": name} for name in runners}}
    monkeypatch.setattr(lint, "_load_config", lambda: config)
    monkeypatch.setattr(lint, "RUNNERS", runners)
    monkeypatch.setattr(sys, "argv", ["lint.py", *files])
    return lint.main()


def test_a_runner_that_raises_fails_the_run(monkeypatch, capsys):
    code = _main(monkeypatch, {"broken": _raising}, ["a.tsx"])
    out, err = capsys.readouterr()
    assert code == 1
    assert ("ERROR broken/runner-failed a.tsx: linter failed to run: "
            "ValueError: unknown extension ?<l at position 4") in out
    assert "1 error(s)" in out
    # the traceback is still printed for whoever debugs the linter
    assert "[lint] broken failed: unknown extension" in err
    assert "Traceback (most recent call last)" in err


def test_the_message_counts_the_files_the_linter_skipped(monkeypatch, capsys):
    assert _main(monkeypatch, {"broken": _raising}, ["a.tsx", "b.tsx", "c.tsx"]) == 1
    assert "ERROR broken/runner-failed a.tsx (+2 more): " in capsys.readouterr().out


def test_other_linters_still_run_after_one_raises(monkeypatch, capsys):
    code = _main(monkeypatch, {"broken": _raising, "style": _one_warning}, ["a.tsx"])
    out, _ = capsys.readouterr()
    assert code == 1
    assert "WARNING style/style a.tsx:3: a warning" in out
    assert "1 error(s), 1 warning(s), 2 total message(s)" in out


@pytest.mark.parametrize("runners", [{"style": _clean}, {"style": _one_warning}])
def test_a_run_without_errors_still_exits_0(monkeypatch, capsys, runners):
    assert _main(monkeypatch, runners, ["a.tsx"]) == 0
