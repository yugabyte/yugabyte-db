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

"""Tests for src/lint/upstream_file.py's fetch_url: which failures it retries and how long it
waits."""

import email.message
import http.client
import importlib.util
import io
import socket
import ssl
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
    """urlopen answers from `server.answers` in order; sleeps are recorded, not slept.

    An answer is the body, an exception urlopen raises, or a _ReadFailure whose exception the
    response's read() raises after urlopen returned.
    """

    class Server:
        answers = []
        calls = 0
        sleeps = []

    def urlopen(url, timeout):
        answer = Server.answers[Server.calls]
        Server.calls += 1
        if isinstance(answer, Exception):
            raise answer
        return _Response(answer.error if isinstance(answer, _ReadFailure) else answer)

    monkeypatch.setattr(upstream_file.urllib.request, "urlopen", urlopen)
    monkeypatch.setattr(upstream_file.time, "sleep", Server.sleeps.append)
    return Server


class _ReadFailure:
    def __init__(self, error):
        self.error = error


@pytest.mark.parametrize("failure", [
    urllib.error.URLError(TimeoutError(110, "Connection timed out")),
    urllib.error.URLError(socket.timeout("timed out")),
    urllib.error.URLError(socket.gaierror(-3, "Temporary failure in name resolution")),
    urllib.error.URLError(ConnectionRefusedError(111, "Connection refused")),
    _ReadFailure(TimeoutError("The read operation timed out")),
    _ReadFailure(ConnectionResetError(104, "Connection reset by peer")),
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


@pytest.mark.parametrize("failure", [
    urllib.error.URLError(ssl.SSLCertVerificationError(1, "certificate verify failed")),
    urllib.error.URLError(OSError("Tunnel connection failed: 407 Proxy Authentication Required")),
    http.client.InvalidURL("URL can't contain control characters"),
    ValueError("unknown url type"),
])
def test_a_failure_no_retry_can_fix_fails_at_once(server, capsys, failure):
    server.answers = [failure]
    with pytest.raises(type(failure)):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1
    assert server.sleeps == []
    assert capsys.readouterr().out.startswith(type(failure).__name__ + ": ")


@pytest.mark.parametrize("code,retry_after,wait", [
    (429, "7", 7.0),
    (503, " 7 ", 7.0),
    (429, "30", upstream_file.RETRY_AFTER_CAP_S),
    # not a number of seconds, so the normal backoff
    (429, "Thu, 01 Jan 1970 00:00:00 GMT", upstream_file.FETCH_BACKOFF_S),
    (429, "soon", upstream_file.FETCH_BACKOFF_S),
    (429, "²",upstream_file.FETCH_BACKOFF_S),  # isdigit() is true, float() fails
    (429, "-1", upstream_file.FETCH_BACKOFF_S),
    (500, "7", upstream_file.FETCH_BACKOFF_S),  # only 429 and 503 read it
])
def test_retry_after_sets_the_wait(server, code, retry_after, wait):
    server.answers = [_http_error(code, retry_after), b"contents"]
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert server.sleeps == [wait]


@pytest.mark.parametrize("code", [429, 503])
def test_a_retry_after_above_the_cap_gives_up(server, capsys, code):
    server.answers = [_http_error(code, "3600"), b"contents"]
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1
    assert server.sleeps == []
    assert capsys.readouterr().out.startswith("HTTPError: ")
