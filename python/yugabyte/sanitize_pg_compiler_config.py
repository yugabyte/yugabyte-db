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
Strip build-machine-private switches from the compiler configuration an installed PostgreSQL hands
to extension authors (pg_config --cflags/--cppflags/--cc and the installed Makefile.global).

The YB build bakes absolute build-tree include paths, the build's compiler wrapper, and
build-machine-only path-remap and libc++ flags into those values.  None of them are valid on an
external machine: the -I paths are dangling, the wrapper does not exist, and the -fdebug-prefix-map
and -stdlib=/libc++ flags point at build-machine locations.  The fixed ISA baseline
(-march/-mtune/-mcpu) is intentionally kept: it is a portable minimum-CPU contract set in
CMakeLists.txt, not derived from the build machine (no -march=native), so an extension should target
the same baseline.

Stripping the thirdparty -I is safe even though a few installed server headers reach a thirdparty
library (ICU via <unicode/...>, OpenSSL/GSSAPI via <openssl/...>/<gssapi.h>): those are
angle-bracket includes the consumer resolves from its own system packages, so only YB's dangling
vendored copy is removed.  An extension needing exact-version parity instead builds against the
matching yugabyte-db-thirdparty.

This module is the single source of truth for the strip rules, shared by the two places those values
are produced:
- src/postgres/src/common/Makefile, which bakes the values into the pg_config binary at build time
  (it invokes this as a CLI via the YB_PG_COMPILER_CONFIG_SANITIZER make variable that
  build_postgres.py sets).
- the release packager (library_packager.py), which rewrites the shipped Makefile.global (it imports
  the functions below).

The rules only DROP recognized build-private tokens.  Everything else is preserved verbatim,
including make-variable references like $(YB_PREPEND_CFLAGS) and -D/-W/-O/-std/sanitizer flags.
Some of those are compiler-family-specific (for example clang-only warning flags), and keeping them
is intentional: the goal is to drop build-machine-private values, not to make the flags
compiler-agnostic, so a consumer builds with the same compiler family YB used (-DYB_COMPILER_TYPE).
"""

import argparse
import os
import sys
from typing import Dict, List


# Include-dir switches whose argument is a path (both glued "-I/path" and separate "-I /path" forms
# appear).
_INCLUDE_SWITCHES = ('-I', '-isystem', '-iquote', '-idirafter')

# Switches dropped by prefix match.  Each is a build-machine-private choice that is wrong or
# meaningless once the package is installed elsewhere.
_DROP_PREFIXES = (
    # remapping of build-tree paths
    '-fdebug-prefix-map=', '-ffile-prefix-map=',
    # the libc++ stdlib selection, dropped along with the build's libc++ include dir (also stripped,
    # as an -I under a build-machine dir): without those headers, the selection would break an
    # external C++ build, so dropping it falls back to the system default stdlib.  -nostdinc++ in
    # _DROP_EXACT is the exact-match half of this same selection.
    '-stdlib=',
    # paths into the build's sanitizer denylist (sanitizer builds only; absent from release
    # packages)
    '-fsanitize-blacklist=', '-fsanitize-ignorelist=',
)

# Switches dropped on an exact match.  -nostdinc++ is the other half of the libc++ stdlib selection
# (see -stdlib= above).
_DROP_EXACT = frozenset(['-nostdinc++'])


def _is_under(path: str, dirs: List[str]) -> bool:
    if not os.path.isabs(path):
        return False
    return any(path == d or path.startswith(d.rstrip('/') + '/') for d in dirs)


def sanitize_flags(tokens: List[str], build_machine_dirs: List[str]) -> List[str]:
    """Drop build-private include dirs and host-specific switches from a flag token list.
    build_machine_dirs are absolute directory prefixes that exist only on the build machine (the
    thirdparty dir, the YB source root, the YB build root); an -I path under any of them is dropped.
    """
    out: List[str] = []
    it = iter(tokens)
    for tok in it:
        # Separate form: "-I" "/abs/path".  The path is the switch's argument, so it is kept or
        # dropped together with the switch and is never reclassified on its own.
        if tok in _INCLUDE_SWITCHES:
            arg = next(it, None)
            if arg is None:  # malformed trailing switch (can't happen for real flags); keep as-is
                out.append(tok)
            elif not _is_under(arg, build_machine_dirs):
                out.extend((tok, arg))
            continue
        # Glued form: "-I/abs/path", "-isystem/abs/path", ...
        glued = next((sw for sw in _INCLUDE_SWITCHES
                      if tok.startswith(sw) and len(tok) > len(sw)), None)
        if glued is not None and _is_under(tok[len(glued):], build_machine_dirs):
            continue
        if tok in _DROP_EXACT or tok.startswith(_DROP_PREFIXES):
            continue
        out.append(tok)
    return out


def sanitize_cc(tokens: List[str]) -> List[str]:
    """Reduce a baked compiler command (e.g. the build's wrapper path) to a plain compiler name,
    since the wrapper path exists only on the build machine and external authors set CC themselves
    anyway.
    """
    return [os.path.basename(t) if os.path.isabs(t) else t for t in tokens]


def assert_sanitized(
        subject: str,
        compiler_commands: Dict[str, str],
        flag_values: Dict[str, str],
        build_machine_dirs: List[str]) -> None:
    """Raise RuntimeError if any already-sanitized value still carries build-machine-private
    content: a compiler command (CC/CXX) left as a path rather than a bare command, or a flag value
    still containing a build-machine directory.  This is the verification twin of
    sanitize_cc/sanitize_flags, shared by both post-sanitization guards -- pg_config at build time
    (build_postgres.py) and the shipped Makefile.global at release time (library_packager.py) -- so
    a regression in either path is reported identically.  subject names the artifact in the error;
    compiler_commands and flag_values each map a value's reader-facing label (e.g. "--cc" or
    "ICU_CFLAGS") to the value.
    """
    problems = []
    for label, value in compiler_commands.items():
        if '/' in value:
            problems.append(f'{label} is a path, not a bare command: {value!r}')
    for label, value in flag_values.items():
        for build_machine_dir in build_machine_dirs:
            if build_machine_dir in value:
                problems.append(
                    f'{label} still contains the build-machine path {build_machine_dir}: '
                    f'{value!r}')
    if problems:
        raise RuntimeError(
            f'{subject} still exports build-machine-private values; the compiler-flag sanitizer '
            '(sanitize_pg_compiler_config.py) did not run or has regressed:\n  '
            + '\n  '.join(problems))


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        # The flags to sanitize look like options themselves (-I, -fdebug-prefix-map, ...).
        # allow_abbrev=False keeps argparse from mistaking one for a prefix of an option below, and
        # parse_known_args collects them as the remainder.
        allow_abbrev=False,
        description='Strip build-machine-private switches from PostgreSQL compiler flags before an '
                    'installed package exports them.  The flags to sanitize are the remaining '
                    'arguments; the sanitized flags are written to stdout.')
    parser.add_argument(
        '--cc', action='store_true',
        help='treat the arguments as a compiler command (reduce an absolute wrapper path to its '
             'basename) rather than a flag list')
    parser.add_argument(
        '--build-machine-dir', action='append', default=[], metavar='DIR',
        help='an absolute directory that exists only on the build machine; include switches '
             'pointing under it are dropped (repeatable)')
    args, flags = parser.parse_known_args(argv[1:])
    result = sanitize_cc(flags) if args.cc else sanitize_flags(flags, args.build_machine_dir)
    sys.stdout.write(' '.join(result))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
