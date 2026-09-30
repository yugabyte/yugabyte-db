#!/usr/bin/env python3

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

"""
Maintains the sanitizer ignorelist of code under src/postgres that is identical to its upstream.

ASAN builds compile src/postgres with this list (-fsanitize-ignorelist), so AddressSanitizer and
UndefinedBehaviorSanitizer only check code that YugabyteDB wrote or changed.  A file identical to
upstream is listed with src:, and a function identical to upstream in a changed file with fun:.

"Upstream" is the release of the upstream project, not the yugabyte/postgres pin in
src/lint/upstream_repositories.csv: the pin carries YugabyteDB's point-imports, and a botched import
is YugabyteDB code.  Extensions pinned to a YugabyteDB fork are not listed at all.

Each listed file and function records a hash of its contents, so that the check subcommand, run by
lint, can tell without the upstream sources whether listed code has since changed.

  generate: rewrite the list; fetches the upstream sources, so needs network access.
  check <path>...: report listed code in the given files that no longer matches the list.
"""

import argparse
import csv
import hashlib
import os
import re
import subprocess
import sys
import tempfile

from typing import Dict, Iterator, List, Optional, Set, Tuple


REPO_ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), '..', '..'))
LIST_PATH = os.path.join(REPO_ROOT, 'build-support', 'pg_upstream_sanitizer_ignorelist.txt')
UPSTREAM_CSV_PATH = os.path.join(REPO_ROOT, 'src', 'lint', 'upstream_repositories.csv')

PG_LOCAL_PATH = 'src/postgres'
PG_REPOSITORY = 'https://github.com/postgres/postgres'
# The PostgreSQL release that src/postgres is based on.  Update on a PG merge.
PG_BASE_REF = 'REL_15_12'

# Extensions that are not compiled in ASAN builds, or have no C code to instrument.
SKIPPED_EXTENSIONS = {'documentdb', 'pg_duckdb', 'pgrx', 'pg_parquet'}

SOURCE_SUFFIXES = ('.c', '.h')
HASH_LEN = 12

# A function definition in the PostgreSQL style: the name starts a line and is followed by the
# parameter list, and the body is delimited by braces at the start of a line.
FUNCTION_START_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\(')


def content_hash(text: str) -> str:
    return hashlib.sha1(text.encode('utf-8', errors='surrogateescape')).hexdigest()[:HASH_LEN]


def parse_functions(text: str) -> Dict[str, str]:
    """
    Returns the definitions in C source text, by function name.  A definition's text includes the
    return type line before its name.  A name defined more than once (e.g. under different #ifdefs)
    is omitted, since it cannot be matched to one upstream definition.
    """
    lines = text.split('\n')
    result: Dict[str, str] = {}
    duplicates: Set[str] = set()
    i = 0
    while i < len(lines):
        match = FUNCTION_START_RE.match(lines[i])
        if not match or i == 0 or not lines[i - 1].strip() or lines[i - 1].rstrip().endswith(';'):
            i += 1
            continue
        # Find the opening brace of the body; a declaration ends with ';' first.
        j = i
        while j < len(lines) and lines[j] != '{' and not lines[j].rstrip().endswith(';'):
            j += 1
        if j == len(lines) or lines[j] != '{':
            i += 1
            continue
        k = j + 1
        while k < len(lines) and lines[k] != '}':
            k += 1
        if k == len(lines):
            break
        name = match.group(1)
        if name in result:
            duplicates.add(name)
        result[name] = '\n'.join(lines[i - 1:k + 1])
        i = k + 1
    for name in duplicates:
        del result[name]
    return result


def git(*args: str, cwd: str = REPO_ROOT) -> str:
    return subprocess.check_output(['git', *args], cwd=cwd, text=True, errors='surrogateescape')


def list_sources(local_prefix: str) -> List[str]:
    """Returns the tracked C sources under local_prefix, relative to the repository root."""
    paths = git('ls-files', '--', local_prefix).splitlines()
    return [path for path in paths if path.endswith(SOURCE_SUFFIXES)]


def read_local(path: str) -> str:
    with open(os.path.join(REPO_ROOT, path), encoding='utf-8', errors='surrogateescape') as f:
        return f.read()


class Upstream:
    """A shallow clone of an upstream repository at a given ref."""

    def __init__(self, repository: str, ref: str, cache_dir: str) -> None:
        self.dir = os.path.join(cache_dir, re.sub(r'[^A-Za-z0-9]+', '_', f'{repository}_{ref}'))
        if not os.path.isdir(self.dir):
            os.makedirs(self.dir)
            git('init', '-q', cwd=self.dir)
            git('fetch', '-q', '--depth=1', repository, ref, cwd=self.dir)
            git('checkout', '-q', 'FETCH_HEAD', cwd=self.dir)

    def read(self, path: str) -> Optional[str]:
        full_path = os.path.join(self.dir, path)
        if not os.path.isfile(full_path):
            return None
        with open(full_path, encoding='utf-8', errors='surrogateescape') as f:
            return f.read()


def upstream_trees() -> Iterator[Tuple[str, str, str]]:
    """Yields (local prefix, repository, ref) for each tree to compare with its upstream."""
    yield PG_LOCAL_PATH, PG_REPOSITORY, PG_BASE_REF
    with open(UPSTREAM_CSV_PATH) as f:
        for row in csv.DictReader(f):
            local_path = row['local_path']
            name = local_path.rsplit('/', 1)[-1]
            if (not local_path.startswith(f'{PG_LOCAL_PATH}/third-party-extensions/') or
                    row['upstream_path'] or name in SKIPPED_EXTENSIONS or
                    row['commit'] == 'TODO' or
                    row['repository'].startswith('https://github.com/yugabyte/')):
                continue
            yield local_path, row['repository'], row['commit']


def owning_tree(path: str, trees: List[Tuple[str, str, str]]) -> Tuple[str, str, str]:
    """Returns the innermost tree containing path."""
    return max((t for t in trees if path.startswith(t[0] + '/')), key=lambda t: len(t[0]))


def generate(cache_dir: str) -> None:
    trees = list(upstream_trees())
    upstreams = {prefix: Upstream(repository, ref, cache_dir) for prefix, repository, ref in trees}
    # Extensions pinned to a fork (or skipped) must not be compared with the PG tree.
    forked = fork_prefixes()

    listed_files: List[Tuple[str, str]] = []
    unchanged_functions: Dict[str, List[Tuple[str, str]]] = {}  # name -> [(path, hash)]
    defined_elsewhere: Set[str] = set()  # names also defined in code that stays instrumented

    for path in list_sources(PG_LOCAL_PATH):
        local = read_local(path)
        local_functions = parse_functions(local)
        prefix = owning_tree(path, trees)[0]
        upstream_text = None
        if not any(path.startswith(p + '/') for p in forked):
            upstream_text = upstreams[prefix].read(path[len(prefix) + 1:])
        if upstream_text is None:
            defined_elsewhere.update(local_functions)
            continue
        if upstream_text == local:
            listed_files.append((path, content_hash(local)))
            continue
        upstream_functions = parse_functions(upstream_text)
        for name, body in local_functions.items():
            if upstream_functions.get(name) == body:
                unchanged_functions.setdefault(name, []).append((path, content_hash(body)))
            else:
                defined_elsewhere.add(name)

    # fun: matches a name in every file, so list a name only if all its definitions outside the
    # listed files are unchanged too.
    listed_by_path: Dict[str, List[Tuple[str, str]]] = {}
    for name, definitions in unchanged_functions.items():
        if name in defined_elsewhere:
            continue
        for path, body_hash in definitions:
            listed_by_path.setdefault(path, []).append((name, body_hash))

    write_list(listed_files, listed_by_path, trees)


def fork_prefixes() -> List[str]:
    """Returns the local prefixes of extensions pinned to something other than their upstream."""
    prefixes = []
    with open(UPSTREAM_CSV_PATH) as f:
        for row in csv.DictReader(f):
            local_path = row['local_path']
            if (local_path.startswith(f'{PG_LOCAL_PATH}/third-party-extensions/') and
                    not row['upstream_path'] and
                    (row['repository'].startswith('https://github.com/yugabyte/') or
                     row['commit'] == 'TODO' or
                     local_path.rsplit('/', 1)[-1] in SKIPPED_EXTENSIONS)):
                prefixes.append(local_path)
    return prefixes


def write_list(listed_files: List[Tuple[str, str]],
               listed_by_path: Dict[str, List[Tuple[str, str]]],
               trees: List[Tuple[str, str, str]]) -> None:
    out = [
        '# Generated by python/yugabyte/pg_upstream_sanitizer_ignorelist.py generate; do not edit.',
        '# Code under src/postgres that is identical to its upstream release, which ASAN builds',
        '# leave uninstrumented.  See the script for details.',
        '#',
    ]
    out += [f'# base {prefix} {repository} {ref}' for prefix, repository, ref in trees]
    for path, file_hash in sorted(listed_files):
        out.append(f'# file {path} {file_hash}')
        out.append(f'src:*/{path}')
    for path in sorted(listed_by_path):
        functions = sorted(listed_by_path[path])
        out.append(f'# funcs {path} ' + ' '.join(f'{n}={h}' for n, h in functions))
        out += [f'fun:{name}' for name, _ in functions]
    with open(LIST_PATH, 'w') as f:
        f.write('\n'.join(out) + '\n')


def read_list() -> Tuple[Dict[str, str], Dict[str, Dict[str, str]]]:
    """Returns the listed files and functions: {path: hash}, {path: {name: hash}}."""
    files: Dict[str, str] = {}
    functions: Dict[str, Dict[str, str]] = {}
    with open(LIST_PATH) as f:
        for line in f:
            fields = line.split()
            if len(fields) == 4 and fields[:2] == ['#', 'file']:
                files[fields[2]] = fields[3]
            elif len(fields) >= 3 and fields[:2] == ['#', 'funcs']:
                functions[fields[2]] = dict(field.split('=', 1) for field in fields[3:])
    return files, functions


def check(paths: List[str]) -> List[str]:
    """Returns lint messages for listed code in the given files that has changed."""
    files, functions = read_list()
    messages = []
    fix = 'run python/yugabyte/pg_upstream_sanitizer_ignorelist.py generate'
    for path in paths:
        if path in files:
            if content_hash(read_local(path)) != files[path]:
                messages.append(
                    f'error:stale_sanitizer_ignorelist:The file is listed in '
                    f'build-support/pg_upstream_sanitizer_ignorelist.txt as identical to upstream '
                    f'but has changed; {fix}:1:')
        elif path in functions:
            local_functions = parse_functions(read_local(path))
            for name, body_hash in sorted(functions[path].items()):
                body = local_functions.get(name)
                if body is None or content_hash(body) != body_hash:
                    messages.append(
                        f'error:stale_sanitizer_ignorelist:Function {name} is listed in '
                        f'build-support/pg_upstream_sanitizer_ignorelist.txt as identical to '
                        f'upstream but has changed; {fix}:1:')
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest='command', required=True)
    generate_parser = subparsers.add_parser('generate')
    generate_parser.add_argument('--cache-dir', help='Where to keep the upstream clones')
    check_parser = subparsers.add_parser('check')
    check_parser.add_argument('paths', nargs='+')
    args = parser.parse_args()

    if args.command == 'generate':
        if args.cache_dir:
            generate(args.cache_dir)
        else:
            with tempfile.TemporaryDirectory() as cache_dir:
                generate(cache_dir)
        return 0

    for message in check(args.paths):
        print(message)
    return 0


if __name__ == '__main__':
    sys.exit(main())
