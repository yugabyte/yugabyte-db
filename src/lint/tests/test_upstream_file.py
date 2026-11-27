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

"""Tests for src/lint/upstream_file.py's fetch_url: which failures it tries again, and how long
it waits.

Run with `python3 -m pytest src/lint/tests/`.  Not wired into CI yet (#38807).
"""

import email.message
import errno
import http.client
import io
import socket
import ssl
import sys
import urllib.error
from pathlib import Path

import pytest

# src/lint, so that this file imports the real module on its own, as Jenkins' doctest import does.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import upstream_file  # noqa: E402


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


def _http_error(code, fp=None):
    return urllib.error.HTTPError("https://x", code, "msg", email.message.Message(), fp)


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
    urllib.error.URLError(OSError(errno.ENETUNREACH, "Network is unreachable")),
    urllib.error.URLError(OSError(errno.EHOSTUNREACH, "No route to host")),
    urllib.error.URLError(ssl.SSLEOFError(8, "EOF occurred in violation of protocol")),
    _ReadFailure(ssl.SSLEOFError(8, "EOF occurred in violation of protocol")),
    _ReadFailure(http.client.IncompleteRead(b"partial", 10)),
    urllib.error.URLError(OSError("Tunnel connection failed: 503 Service Unavailable")),
    _http_error(408),
    _http_error(500),
    _http_error(503),
])
def test_a_transient_failure_is_tried_again(server, failure):
    server.answers = [failure, b"contents"]
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert server.calls == 2
    assert server.sleeps == [upstream_file.FETCH_BACKOFF_S]


def test_the_wait_doubles_and_the_last_failure_is_raised(server, capsys):
    server.answers = [_http_error(502), _http_error(503), _http_error(504)]
    with pytest.raises(urllib.error.HTTPError) as raised:
        upstream_file.fetch_url("https://x")
    assert raised.value is server.answers[-1]
    assert server.calls == upstream_file.FETCH_ATTEMPTS == 3
    assert server.sleeps == [1.0, 2.0]
    # one line on stdout, so arc lint shows why the file failed
    assert capsys.readouterr().out == "HTTPError: HTTP Error 504: msg\n"


def test_a_5xx_response_is_closed_before_the_wait(server):
    response = io.BytesIO(b"Service Unavailable")
    server.answers = [_http_error(503, response), b"contents"]
    assert upstream_file.fetch_url("https://x") == b"contents"
    assert response.closed


def test_a_429_is_transient_but_not_tried_again(server):
    error = _http_error(429)
    assert upstream_file._is_transient(error)
    server.answers = [error]
    with pytest.raises(urllib.error.HTTPError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1


def test_a_404_is_not_found_without_a_retry(server):
    server.answers = [_http_error(404)]
    with pytest.raises(upstream_file.NotFoundError):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1


@pytest.mark.parametrize("code", [400, 401, 403, 429])
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
def test_a_failure_that_is_not_transient_fails_at_once(server, capsys, failure):
    server.answers = [failure]
    with pytest.raises(type(failure)):
        upstream_file.fetch_url("https://x")
    assert server.calls == 1
    assert server.sleeps == []
    assert capsys.readouterr().out.startswith(type(failure).__name__ + ": ")
