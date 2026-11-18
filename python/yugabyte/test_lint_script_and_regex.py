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

"""Tests for build-support/lint.py's script-and-regex runner and src/lint/upstream_file.py's
fetch retry: a linter script that stops partway is reported, and a transient fetch failure is
retried."""

import importlib.util
import io
import sys
import urllib.error
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


lint = _load("yb_lint", REPO_ROOT / "build-support" / "lint.py")
sys.path.insert(0, str(REPO_ROOT / "src" / "lint"))
upstream_file = _load("upstream_file", REPO_ROOT / "src" / "lint" / "upstream_file.py")

FINDING_REGEX = "/^(?P<severity>warning|error):(?P<name>[^:]+):(?P<message>[^:]+):(?P<line>\\d+)$/m"


def _run(tmp_path, body, exits_on_findings=None):
    script = tmp_path / "stub.sh"
    script.write_text("#!/usr/bin/env bash\n" + body + "\n")
    script.chmod(0o755)
    config = {"script-and-regex.script": str(script), "script-and-regex.regex": FINDING_REGEX}
    if exits_on_findings is not None:
        config["script-and-regex.exits-nonzero-on-findings"] = exits_on_findings
    linter = lint.Linter.build("stub", config, [])
    return lint.run_script_and_regex(linter, ["some/file.c"])


def _codes(msgs):
    return [m.code for m in msgs]


def test_findings_with_exit_zero_are_reported(tmp_path):
    msgs = _run(tmp_path, "echo 'warning:bad_style:a message:12'\nexit 0")
    assert _codes(msgs) == ["bad_style"]
    assert msgs[0].line == 12


def test_a_silent_failure_is_reported(tmp_path):
    msgs = _run(tmp_path, "echo 'ctags: command not found' >&2\nexit 1")
    assert _codes(msgs) == ["script-failed"]
    assert "status 1" in msgs[0].message
    assert "ctags: command not found" in msgs[0].message


def test_a_failure_after_findings_is_reported_with_them(tmp_path):
    # postgres.sh reports the early rules' findings, then `check_ctags || exit 1` stops it:
    # the rules after that never ran, so the file must not pass on the findings alone.
    msgs = _run(tmp_path, "echo 'warning:missing_yb_marker:early rule:3'\nexit 1")
    assert sorted(_codes(msgs)) == ["missing_yb_marker", "script-failed"]


def test_a_script_that_exits_nonzero_on_findings_reports_only_them(tmp_path):
    msgs = _run(tmp_path, "echo 'error:version:mismatch:1'\nexit 1", exits_on_findings=True)
    assert _codes(msgs) == ["version"]


def test_a_script_that_exits_nonzero_on_findings_still_fails_without_them(tmp_path):
    msgs = _run(tmp_path, "exit 2", exits_on_findings=True)
    assert _codes(msgs) == ["script-failed"]


def test_a_clean_file_reports_nothing(tmp_path):
    assert _run(tmp_path, "exit 0") == []


class _Body(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def _urlopen_answering(*answers):
    calls = []

    def urlopen(url, timeout):
        calls.append(url)
        answer = answers[len(calls) - 1]
        if isinstance(answer, Exception):
            raise answer
        return _Body(answer)

    return urlopen, calls


def _http_error(code):
    return urllib.error.HTTPError("https://x", code, "msg", {}, None)


@pytest.fixture(autouse=True)
def _no_sleep(monkeypatch):
    monkeypatch.setattr(upstream_file.time, "sleep", lambda s: None)


def test_a_transient_fetch_failure_is_retried(monkeypatch):
    urlopen, calls = _urlopen_answering(
        urllib.error.URLError("timed out"), _http_error(503), b"contents")
    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert len(calls) == 3


def test_a_fetch_that_keeps_failing_raises_after_the_last_attempt(monkeypatch):
    urlopen, calls = _urlopen_answering(*[TimeoutError("read timed out")] * 3)
    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    with pytest.raises(TimeoutError):
        upstream_file.fetch_url("https://x")
    assert len(calls) == upstream_file.FETCH_ATTEMPTS


def test_a_404_is_not_found_without_a_retry(monkeypatch):
    urlopen, calls = _urlopen_answering(_http_error(404))
    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    with pytest.raises(upstream_file.NotFoundError):
        upstream_file.fetch_url("https://x")
    assert len(calls) == 1


def test_a_403_is_raised_without_a_retry(monkeypatch):
    urlopen, calls = _urlopen_answering(_http_error(403))
    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert len(calls) == 1
