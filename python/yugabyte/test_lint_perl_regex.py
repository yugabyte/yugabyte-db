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

"""Tests for build-support/lint.py's _parse_perl_regex: the Perl/PCRE named-group syntax that
arcanist accepts, (?<name>...), compiles under Python's re, and nothing else in a pattern changes."""

import importlib.util
import json
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


lint = _load("yb_lint_perl_regex", REPO_ROOT / "build-support" / "lint.py")


def test_perl_named_groups_compile_and_capture():
    pat = lint._parse_perl_regex("/^(?<line>[0-9]+):(?<name>[a-z-]+)$/m")
    assert pat.pattern == "^(?P<line>[0-9]+):(?P<name>[a-z-]+)$"
    assert pat.search("12:no-unused-vars").groupdict() == {"line": "12", "name": "no-unused-vars"}


def test_python_named_groups_are_unchanged():
    assert lint._parse_perl_regex("/(?P<a>x)(?<b>y)/").pattern == "(?P<a>x)(?P<b>y)"


@pytest.mark.parametrize("body", [
    "(?<=a)b",    # lookbehind
    "(?<!a)b",    # negative lookbehind
    r"\(?<x>",    # an escaped paren: a literal "(", then an optional "?", not a group
])
def test_other_constructs_are_left_alone(body):
    assert lint._parse_perl_regex(f"/{body}/").pattern == body


def test_an_escaped_backslash_before_a_group_still_rewrites_the_group():
    # \\ is a literal backslash, so the ( after it opens a group.
    assert lint._parse_perl_regex(r"/\\(?<x>y)/").pattern == r"\\(?P<x>y)"


def test_eslint_ui_regex_reads_eslint_stylish_output():
    with open(REPO_ROOT / ".arclint", encoding="utf-8") as f:
        raw = json.load(f)["linters"]["eslint-ui"]["script-and-regex.regex"]
    pat = lint._parse_perl_regex(raw)
    out = ("/repo/managed/ui/src/a.tsx\n"
           "  12:5  error  'x' is assigned a value but never used  no-unused-vars\n")
    gd = next(pat.finditer(out)).groupdict()
    assert gd["line"] == "12"
    assert gd["name"] == "no-unused-vars"


def test_every_configured_regex_compiles():
    with open(REPO_ROOT / ".arclint", encoding="utf-8") as f:
        linters = json.load(f)["linters"]
    for name, cfg in linters.items():
        if cfg.get("type") == "script-and-regex":
            lint._parse_perl_regex(cfg["script-and-regex.regex"])
