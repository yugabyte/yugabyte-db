#!/usr/bin/env python3
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations under
# the License.
#
# Get upstream's version of a file.
# Input: filepath (with respect to yugabyte/yugabyte-db repository) whose upstream counterpart to
# get, and an output filepath to write that counterpart's contents to.
# - Exit 0 for successfully writing the contents.  In case upstream_repositories.csv does not pin
#   a commit for the repository owning the file, the local file's own contents are written,
#   meaning there are no differences from upstream.
# - Exit 1 for exceptions and assertion failures.  For the most part, output the main error message
#   to stdout.  stderr gets the full stack trace that Python naturally provides.
# - Exit 2 when the upstream file or commit is not found.
# If you get exit code 1, you may need to update the list of expected exceptions to be caught.

from pathlib import Path
import csv
import os
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
CSV_FILEPATH = f"{SCRIPT_PATH}/upstream_repositories.csv"

# A remote fetch that fails in a way another attempt can get past is tried up to this many times.
FETCH_ATTEMPTS = 3
# Seconds to wait before the second attempt.  The wait doubles before each later attempt.
FETCH_BACKOFF_S = 1.0
# The longest wait a Retry-After header can ask for.  A fetch whose server asks for longer gives
# up instead of retrying before the server said it could succeed.
RETRY_AFTER_CAP_S = 30.0
FETCH_TIMEOUT_S = 10


class NotFoundError(Exception):
    """The upstream commit or the file within it does not exist."""


class UnpinnedError(Exception):
    """upstream_repositories.csv does not pin a commit for the repository owning the file."""


def resolve(filepath):
    """Return (repository, commit, upstream_filepath) for the given local filepath."""
    with open(CSV_FILEPATH) as f:
        table = csv.DictReader(f)
        # The table is sorted.  Reverse the table so that longer local_path matches first.  For
        # example, rather than matching "src/postgres", match "src/postgres/contrib/passwordcheck"
        # for filepath "src/postgres/contrib/passwordcheck/passwordcheck_extra.c".
        for row in reversed(list(table)):
            # This could use Python's pathlib library, but assuming paths are clean (since this will
            # be called by arc lint), it's simpler this way.
            if filepath.startswith(row['local_path']):
                break
        else:
            raise ValueError(f"Path {filepath} not found in {CSV_FILEPATH}")
    # TODO(jason): use removeprefix when minimum Python version becomes 3.9 or higher.
    assert filepath.startswith(row['local_path']), (filepath, row)
    upstream_filepath = str(Path(row['upstream_path'])
                            / Path(filepath[len(row['local_path']) + 1:]))
    commit = row['commit']

    # In case the "commit" column refers to a different row (by specifying that row's local_path),
    # look up that row's commit.
    if commit.startswith("src/postgres"):
        with open(CSV_FILEPATH) as f:
            table = csv.DictReader(f)
            for row in table:
                if commit == row['local_path']:
                    commit = row['commit']
                    break
            else:
                raise ValueError(f"Path {filepath} not found in {CSV_FILEPATH}")

    return row['repository'], commit, upstream_filepath


def fetch(filepath):
    """Return the upstream counterpart's contents as bytes.

    Raise UnpinnedError when the owning repository has no pinned commit, and NotFoundError when the
    upstream commit or the file within it does not exist.
    """
    repository, commit, upstream_filepath = resolve(filepath)

    # TODO(#26460): update pg_cron commit in upstream_repositories.csv (for now, skip diff).
    if commit == "TODO":
        raise UnpinnedError(filepath)

    # First, attempt local access.
    local_upstream_repo_path = str(Path.home() / "code" / repository.split('/')[-1])
    if os.path.isdir(local_upstream_repo_path):
        p = subprocess.run(["git", "-C", local_upstream_repo_path, "show",
                            f"{commit}:{upstream_filepath}"],
                           stdout=subprocess.PIPE)
        if p.returncode == 0:
            return p.stdout

    # Second, attempt remote access.
    repo_name = '/'.join(repository.split('/')[-2:])
    if repository.startswith("https://github.com"):
        url = f"https://raw.githubusercontent.com/{repo_name}/{commit}/{upstream_filepath}"
    else:
        assert repository.startswith("https://gitlab.com"), repository
        url = f"https://gitlab.com/{repo_name}/-/raw/{commit}/{upstream_filepath}"
    return fetch_url(url)


def fetch_url(url):
    """Return the body at url, retrying a failure that another attempt can get past.

    Raise NotFoundError on a 404.
    """
    for attempt in range(FETCH_ATTEMPTS):
        try:
            # The body is read inside the try, so a timeout while reading it is retried too.
            with urllib.request.urlopen(url, timeout=FETCH_TIMEOUT_S) as req:
                return req.read()
        except Exception as e:
            # Examples:
            # - urllib.error.HTTPError: HTTP Error 403: Forbidden
            # - urllib.error.HTTPError: HTTP Error 404: Not Found
            # - urllib.error.HTTPError: HTTP Error 429: Too Many Requests
            # - urllib.error.HTTPError: HTTP Error 503: Service Unavailable
            # - urllib.error.URLError: <urlopen error [Errno -2] Name or service not known>
            # - urllib.error.URLError: <urlopen error [Errno -3] Temporary failure in name
            #   resolution>
            # - urllib.error.URLError: <urlopen error [Errno 110] Connection timed out>
            # - urllib.error.URLError: <urlopen error timed out>
            # - (older python version) socket.timeout: The read operation timed out
            # - (newer python version) TimeoutError: The read operation timed out

            # A fetch ends in one of these ways:
            # - Retried: a 429, a 5xx, a timeout, a DNS failure, or a refused or reset connection.
            #   It is fatal only once it is out of retries.
            # - Not retried: everything else.  A 404 is NotFoundError, which means the file or
            #   commit does not exist upstream, not that the fetch failed.  Any other error,
            #   including the 403 that gitlab returns for a missing repository (github gives
            #   404), is fatal on the first attempt.
            if isinstance(e, urllib.error.HTTPError) and e.code == 404:
                raise NotFoundError(url) from e

            wait = _retry_wait(e, attempt)
            if wait is not None and attempt < FETCH_ATTEMPTS - 1:
                time.sleep(wait)
                continue

            # The rest, errors that are out of retries or not retryable, should rethrow.  Also
            # print the error message to stdout so that it shows in arc lint stdout (otherwise,
            # it gets buried under stacktrace in stderr, and arc lint truncates it so that it's
            # not seen).
            print(f"{type(e).__name__}: {e}")
            raise
    raise AssertionError("unreachable")


def _retry_wait(e, attempt):
    """Return the seconds to wait before retrying after e, or None if e is not retried."""
    backoff = FETCH_BACKOFF_S * 2 ** attempt
    if isinstance(e, urllib.error.HTTPError):
        if e.code != 429 and e.code < 500:
            return None
        retry_after = _retry_after_s(e) if e.code in (429, 503) else None
        if retry_after is None:
            return backoff
        return retry_after if retry_after <= RETRY_AFTER_CAP_S else None
    # urlopen wraps a failure to connect in a URLError; a failure while reading the body is
    # raised as it is.
    reason = e.reason if isinstance(e, urllib.error.URLError) else e
    if _is_network_failure(reason):
        return backoff
    return None


def _is_network_failure(e):
    """Whether e is a timeout, a DNS failure, or a refused or reset connection.

    Not one of these, so not retried: a TLS certificate error, an invalid URL, or a proxy that
    refuses the tunnel.
    """
    # socket.timeout is TimeoutError from Python 3.10 on.
    return isinstance(e, (TimeoutError, socket.timeout, socket.gaierror, ConnectionError))


def _retry_after_s(e):
    """Return the seconds a Retry-After header asks for, or None if there is no valid one.

    GitHub and GitLab both send it as a number of seconds, so that is the only form read.
    """
    value = (e.headers.get("Retry-After") or "").strip() if e.headers else ""
    if not re.fullmatch(r"[0-9]{1,9}", value):
        return None
    return float(value)


def main(filepath, output_filepath):
    try:
        contents = fetch(filepath)
    except NotFoundError:
        return 2
    except UnpinnedError:
        with open(filepath, 'rb') as f:
            contents = f.read()
    with open(output_filepath, 'wb') as f:
        f.write(contents)
    return 0


if __name__ == "__main__":
    exit(main(*sys.argv[1:]))
