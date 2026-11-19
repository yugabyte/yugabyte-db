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

"""Tests for src/lint/upstream_file.py's fetch_url: which failures it retries, how long it waits,
and the retry budget that build-support/lint.py shares across one lint run."""

import email.message
import importlib.util
import io
import os
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


upstream_file = _load("yb_upstream_file", REPO_ROOT / "src" / "lint" / "upstream_file.py")
lint = _load("yb_lint_fetch_budget", REPO_ROOT / "build-support" / "lint.py")


class _Response(io.BytesIO):
    """A urlopen response whose read() can raise, as a timeout while reading the body does."""

    def __init__(self, body):
        super().__init__(b"" if isinstance(body, Exception) else body)
        self.error = body if isinstance(body, Exception) else None

    def read(self, *args):
        if self.error:
            raise self.error
        return super().read(*args)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def _http_error(code, retry_after=None):
    headers = email.message.Message()
    if retry_after is not None:
        headers["Retry-After"] = retry_after
    return urllib.error.HTTPError("https://x", code, "msg", headers, None)


@pytest.fixture
def server(monkeypatch):
    """urlopen answers from `server.answers` in order; sleeps are recorded, not slept."""

    class Server:
        answers = []
        calls = 0
        sleeps = []

    def urlopen(url, timeout):
        answer = Server.answers[Server.calls]
        Server.calls += 1
        if isinstance(answer, urllib.error.URLError):
            raise answer
        return _Response(answer)

    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    monkeypatch.setattr(upstream_file.time, "sleep", Server.sleeps.append)
    monkeypatch.delenv(upstream_file.RETRY_BUDGET_FILE_ENV, raising=False)
    return Server


@pytest.mark.parametrize("failure", [
    urllib.error.URLError("[Errno 110] Connection timed out"),
    urllib.error.URLError("[Errno -3] Temporary failure in name resolution"),
    TimeoutError("The read operation timed out"),  # raised by read(), after urlopen returned
    _http_error(429),
    _http_error(500),
    _http_error(503),
])
def test_a_failure_another_attempt_can_get_past_is_retried(server, failure):
    server.answers = [failure, b"contents"]
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert server.calls == 2
    assert server.sleeps == [upstream_file.FETCH_BACKOFF_S]


def test_the_wait_doubles_and_the_last_failure_is_raised(server, capsys):
    server.answers = [_http_error(502)] * upstream_file.FETCH_ATTEMPTS
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert server.calls == upstream_file.FETCH_ATTEMPTS == 3
    assert server.sleeps == [1.0, 2.0]
    # one line on stdout, so arc lint shows why the file failed
    assert capsys.readouterr().out == "HTTPError: HTTP Error 502: msg\n"


def test_a_404_is_not_found_without_a_retry(server):
    server.answers = [_http_error(404)]
    with pytest.raises(upstream_file.NotFoundError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1


@pytest.mark.parametrize("code", [400, 401, 403])
def test_another_4xx_fails_without_a_retry(server, code):
    server.answers = [_http_error(code)]
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1


@pytest.mark.parametrize("code,retry_after,wait", [
    (429, "7", 7.0),
    (503, "7", 7.0),
    (429, "3600", upstream_file.RETRY_AFTER_CAP_S),
    (429, "Thu, 01 Jan 1970 00:00:00 GMT", 0.0),  # a time already past
    (429, "soon", upstream_file.FETCH_BACKOFF_S),  # not a valid value
    (500, "7", upstream_file.FETCH_BACKOFF_S),  # only 429 and 503 read it
])
def test_retry_after_sets_the_wait(server, code, retry_after, wait):
    server.answers = [_http_error(code, retry_after), b"contents"]
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert server.sleeps == [wait]


def test_retries_stop_when_the_lint_run_budget_is_spent(server, monkeypatch, tmp_path):
    budget = tmp_path / "budget"
    budget.write_text("")
    monkeypatch.setenv(upstream_file.RETRY_BUDGET_FILE_ENV, str(budget))
    cost = upstream_file.FETCH_BACKOFF_S + upstream_file.FETCH_TIMEOUT_S
    retries_in_budget = int(upstream_file.FETCH_RETRY_BUDGET_S // cost)

    # Each file's fetch fails once, then succeeds, until the budget has no room for a retry.
    for _ in range(retries_in_budget):
        server.answers, server.calls = [_http_error(503), b"contents"], 0
        assert upstream_file.fetch_url("https://x") == b"contents"
    server.answers, server.calls = [_http_error(503), b"contents"], 0
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1
    assert len(server.sleeps) == retries_in_budget


def test_lint_py_gives_its_linter_scripts_one_budget_file(monkeypatch, tmp_path):
    # main() sets the variable in os.environ; give it a copy that is restored afterwards
    monkeypatch.setattr(os, "environ", {k: v for k, v in os.environ.items()
                                        if k != lint.FETCH_RETRY_BUDGET_FILE_ENV})
    seen = []

    def runner(linter, files):
        seen.append(os.environ.get(lint.FETCH_RETRY_BUDGET_FILE_ENV))
        return []

    monkeypatch.setattr(lint, "_load_config", lambda: {"linters": {"a": {"type": "t"},
                                                                    "b": {"type": "t"}}})
    monkeypatch.setattr(lint, "RUNNERS", {"t": runner})
    monkeypatch.setattr(lint.atexit, "register", lambda *args: None)
    monkeypatch.setattr(sys, "argv", ["lint.py", "a.c"])
    assert lint.main() == 0
    assert len(seen) == 2 and seen[0] == seen[1] and Path(seen[0]).is_file()
    assert lint.FETCH_RETRY_BUDGET_FILE_ENV == upstream_file.RETRY_BUDGET_FILE_ENV
    os.unlink(seen[0])
