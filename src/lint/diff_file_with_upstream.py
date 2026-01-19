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
# Diff a file with upstream's version of it.
# Input: filepath (with respect to yugabyte/yugabyte-db repository) to run diff for.
# - Exit 0 for successfully getting a diff (regardless of whether there are differences or not).
#   Output the diff to stdout.
# - Exit 1 for exceptions and assertion failures.  For the most part, output the main error message
#   to stdout.  stderr gets the full stack trace that Python naturally provides.
# - Exit 2 when the upstream file or commit is not found.
# If you get exit code 1, you may need to update the list of expected exceptions to be caught.

from pathlib import Path
import csv
import os
import urllib.request
import subprocess
import sys

SCRIPT_PATH = os.path.dirname(os.path.realpath(__file__))
CSV_FILEPATH = f"{SCRIPT_PATH}/upstream_repositories.csv"


def main(filepath, *diff_options):
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

    # TODO(#26460): update pg_cron commit in upstream_repositories.csv (for now, skip diff).
    if commit == "TODO":
        return 0

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

    # First, attempt local diffing.
    local_upstream_repo_path = str(Path.home() / "code" / row['repository'].split('/')[-1])
    if os.path.isdir(local_upstream_repo_path):
        p1 = subprocess.Popen(["git", "-C", local_upstream_repo_path, "show",
                               f"{commit}:{upstream_filepath}"],
                              stdout=subprocess.PIPE)
        p2 = subprocess.Popen(["diff", filepath, "-"] + list(diff_options),
                              stdin=p1.stdout,
                              stdout=subprocess.PIPE)
        p1.stdout.close()   # Allow p1 to receive a SIGPIPE if p2 exits.
        output = p2.communicate()[0]
        if p1.wait() == 0 and p2.returncode in (0, 1):
            sys.stdout.buffer.write(output)
            return 0

    # Second, attempt remote diffing.
    repo_name = '/'.join(row['repository'].split('/')[-2:])
    if row['repository'].startswith("https://github.com"):
        url = f"https://raw.githubusercontent.com/{repo_name}/{commit}/{upstream_filepath}"
    else:
        assert row['repository'].startswith("https://gitlab.com"), row
        url = f"https://gitlab.com/{repo_name}/-/raw/{commit}/{upstream_filepath}"
    try:
        req = urllib.request.urlopen(url, timeout=10)
    except Exception as e:
        # Examples:
        # - urllib.error.HTTPError: HTTP Error 403: Forbidden
        # - urllib.error.HTTPError: HTTP Error 404: Not Found
        # - urllib.error.HTTPError: HTTP Error 503: Service Unavailable
        # - urllib.error.URLError: <urlopen error [Errno -2] Name or service not known>
        # - urllib.error.URLError: <urlopen error [Errno -3] Temporary failure in name resolution>
        # - urllib.error.URLError: <urlopen error [Errno 110] Connection timed out>
        # - urllib.error.URLError: <urlopen error timed out>
        # - (older python version) socket.timeout: The read operation timed out
        # - (newer python version) TimeoutError: The read operation timed out

        # Only 404 should be considered expected.  That happens if the file path or commit does not
        # exist.  403 happens for gitlab when the repository does not exist (github gives 404), and
        # that should be considered a fatal issue.
        if isinstance(e, urllib.error.HTTPError) and e.code == 404:
            return 2

        # The rest should rethrow.  Also print the error message to stdout so that it shows in arc
        # lint stdout (otherwise, it gets buried under stacktrace in stderr, and arc lint truncates
        # it so that it's not seen).
        print(f"{type(e).__name__}: {e}")
        raise

    p = subprocess.Popen(["diff", filepath, "-"],
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE)
    p.communicate(req.read())
    output = p.communicate()[0]
    if p.returncode in (0, 1):
        sys.stdout.buffer.write(output)
        return 0

    raise ValueError(f"Failed to diff for {filepath}")


if __name__ == "__main__":
    exit(main(*sys.argv[1:]))
