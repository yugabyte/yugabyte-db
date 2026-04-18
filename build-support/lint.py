#!/usr/bin/env python3
#
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
#
# YugabyteDB linter: reads .lint and runs the configured linters on changed files (or files
# passed on the command line).

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CPPLINT = (shutil.which("cpplint.py")
                   or str(REPO_ROOT / "thirdparty/installed/bin/cpplint.py"))


@dataclass
class Message:
    path: str
    line: int | None
    severity: str
    code: str
    message: str
    linter: str

    def format(self) -> str:
        loc = f":{self.line}" if self.line else ""
        return f"{self.severity.upper()} {self.linter}/{self.code} {self.path}{loc}: {self.message}"


@dataclass
class Linter:
    name: str
    config: dict
    includes: list[re.Pattern] = field(default_factory=list)
    excludes: list[re.Pattern] = field(default_factory=list)

    @classmethod
    def build(cls, name: str, config: dict, global_excludes: list[re.Pattern]) -> "Linter":
        includes = _compile_patterns(config.get("include", []))
        excludes = list(global_excludes) + _compile_patterns(config.get("exclude", []))
        return cls(name=name, config=config, includes=includes, excludes=excludes)

    def matches(self, path: str) -> bool:
        if self.includes and not any(p.search(path) for p in self.includes):
            return False
        return not any(p.search(path) for p in self.excludes)


def _compile_patterns(raw) -> list[re.Pattern]:
    if raw is None:
        return []
    if isinstance(raw, str):
        raw = [raw]
    return [re.compile(p) for p in raw]


def _git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=REPO_ROOT, text=True).strip()


def _changed_files() -> list[str]:
    """Files modified in the working copy plus files changed on this branch vs its base.

    Base resolution tries @{upstream}, then origin/master, then falls back to HEAD (so only
    uncommitted changes are considered).
    """
    base: str | None = None
    for candidate in ("@{upstream}", "origin/master"):
        try:
            _git("rev-parse", "--verify", "--quiet", candidate)
            base = candidate
            break
        except subprocess.CalledProcessError:
            continue
    ref = base or "HEAD"
    committed = _git("diff", "--name-only", "--diff-filter=d", ref).splitlines()
    uncommitted = _git("diff", "--name-only", "--diff-filter=d", "HEAD").splitlines()
    untracked = _git("ls-files", "--others", "--exclude-standard").splitlines()
    seen: set[str] = set()
    result: list[str] = []
    for path in committed + uncommitted + untracked:
        if path and path not in seen:
            seen.add(path)
            result.append(path)
    return result


def _load_config() -> dict:
    with open(REPO_ROOT / ".lint") as f:
        return json.load(f)


def _normalize_path(path: str) -> str:
    p = Path(path)
    if p.is_absolute():
        try:
            p = p.relative_to(REPO_ROOT)
        except ValueError:
            return str(p)
    return str(p)


# --- Linter runners --------------------------------------------------------------------------


def run_text(linter: Linter, files: list[str]) -> list[Message]:
    max_len = int(linter.config.get("text.max-line-length", 80))
    severity = linter.config.get("severity", {})
    msgs: list[Message] = []
    for path in files:
        try:
            with open(REPO_ROOT / path, "rb") as f:
                data = f.read()
        except OSError as exc:
            msgs.append(Message(path, None, "error", "read", str(exc), linter.name))
            continue
        if data and not data.endswith(b"\n"):
            if severity.get("3") != "disabled":
                msgs.append(Message(path, None, "warning", "file-no-newline",
                                    "File does not end with newline", linter.name))
        for i, raw in enumerate(data.splitlines(), start=1):
            try:
                line = raw.decode("utf-8")
            except UnicodeDecodeError:
                continue
            if line.rstrip() != line and severity.get("2") != "disabled":
                msgs.append(Message(path, i, "warning", "trailing-whitespace",
                                    "Trailing whitespace", linter.name))
            if len(line) > max_len and severity.get("2") != "disabled":
                msgs.append(Message(path, i, "warning", "line-too-long",
                                    f"Line longer than {max_len} chars ({len(line)})", linter.name))
            if b"\t" in raw and severity.get("2") != "disabled":
                msgs.append(Message(path, i, "advice", "tab-literal",
                                    "Tab character in source", linter.name))
    return msgs


def run_pep8(linter: Linter, files: list[str]) -> list[Message]:
    binary = linter.config.get("bin", "pycodestyle")
    flags = linter.config.get("flags", [])
    msgs: list[Message] = []
    if not shutil.which(binary):
        print(f"[lint] skipping {linter.name}: {binary} not found", file=sys.stderr)
        return msgs
    proc = subprocess.run([binary, *flags, *files], cwd=REPO_ROOT, text=True,
                          capture_output=True)
    pat = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+):\s+(?P<code>\S+)\s+(?P<msg>.*)$")
    for line in proc.stdout.splitlines():
        m = pat.match(line)
        if m:
            msgs.append(Message(m["path"], int(m["line"]), "warning", m["code"], m["msg"],
                                linter.name))
        else:
            print(line, file=sys.stderr)
    return msgs


def run_cpplint(linter: Linter, files: list[str]) -> list[Message]:
    binary = DEFAULT_CPPLINT
    if not os.access(binary, os.X_OK):
        print(f"[lint] skipping {linter.name}: cpplint.py not executable at {binary}",
              file=sys.stderr)
        return []
    max_len = int(linter.config.get("cpplint.max_line_length", 80))
    header_root = linter.config.get("cpplint.header_guard_root_dir", "")
    severity_rules = linter.config.get("severity.rules", {})
    disabled: list[str] = []
    for rule, sev in severity_rules.items():
        if sev == "disabled":
            raw = rule.strip("()^$")
            disabled.append(raw)
    filter_arg = ",".join(f"-{d}" for d in disabled) if disabled else ""
    cmd = [binary, f"--linelength={max_len}"]
    if header_root:
        cmd.append(f"--root={header_root}")
    if filter_arg:
        cmd.append(f"--filter={filter_arg}")
    cmd.extend(files)
    proc = subprocess.run(cmd, cwd=REPO_ROOT, text=True, capture_output=True)
    pat = re.compile(
        r"^(?P<path>.+?):(?P<line>\d+):\s+(?P<msg>.+?)\s+\[(?P<code>[^\]]+)\]\s+\[\d+\]$")
    msgs: list[Message] = []
    for line in proc.stderr.splitlines():
        if line.startswith("Done processing") or line.startswith("Total errors"):
            continue
        m = pat.match(line)
        if m:
            msgs.append(Message(m["path"], int(m["line"]), "error", m["code"], m["msg"],
                                linter.name))
        else:
            print(line, file=sys.stderr)
    return msgs


def run_golint(linter: Linter, files: list[str]) -> list[Message]:
    binary = shutil.which("golint")
    if not binary:
        print(f"[lint] skipping {linter.name}: golint not found", file=sys.stderr)
        return []
    proc = subprocess.run([binary, *files], cwd=REPO_ROOT, text=True, capture_output=True)
    pat = re.compile(r"^(?P<path>.+?):(?P<line>\d+):\d+:\s+(?P<msg>.*)$")
    msgs: list[Message] = []
    for line in proc.stdout.splitlines():
        m = pat.match(line)
        if m:
            msgs.append(Message(m["path"], int(m["line"]), "warning", "golint", m["msg"],
                                linter.name))
    return msgs


def run_script_and_regex(linter: Linter, files: list[str]) -> list[Message]:
    script = linter.config["script-and-regex.script"]
    raw_regex = linter.config["script-and-regex.regex"]
    delim = raw_regex[0]
    end = raw_regex.rfind(delim)
    body = raw_regex[1:end]
    flags_str = raw_regex[end + 1:]
    re_flags = 0
    if "i" in flags_str:
        re_flags |= re.IGNORECASE
    if "s" in flags_str:
        re_flags |= re.DOTALL
    if "m" in flags_str:
        re_flags |= re.MULTILINE
    pat = re.compile(body, re_flags)

    msgs: list[Message] = []
    for path in files:
        proc = subprocess.run(["bash", "-c", f"{script} \"$0\"", path], cwd=REPO_ROOT,
                              text=True, capture_output=True)
        output = (proc.stdout or "") + (proc.stderr or "")
        matches = list(pat.finditer(output)) if re_flags & re.MULTILINE else (
            [pat.search(output)] if pat.search(output) else [])
        for m in matches:
            if m is None:
                continue
            gd = m.groupdict()
            line = gd.get("line")
            msgs.append(Message(
                path,
                int(line) if line and line.isdigit() else None,
                gd.get("severity", "warning"),
                gd.get("name", linter.name),
                gd.get("message", output.strip()),
                linter.name,
            ))
    return msgs


RUNNERS = {
    "text": run_text,
    "pep8": run_pep8,
    "googlecpplint": run_cpplint,
    "golint": run_golint,
    "script-and-regex": run_script_and_regex,
}


# --- Main ------------------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description="YugabyteDB linter.")
    ap.add_argument("paths", nargs="*", help="Files to lint. If omitted, uses changed files.")
    ap.add_argument("--everything", action="store_true",
                    help="Lint all tracked files (ignores paths).")
    ap.add_argument("--only", action="append", default=[],
                    help="Only run linters whose name matches (repeatable).")
    ap.add_argument("--list-linters", action="store_true", help="List configured linters and exit.")
    args = ap.parse_args()

    cfg = _load_config()
    global_excludes = _compile_patterns(cfg.get("exclude", []))
    linters = [Linter.build(name, lc, global_excludes)
               for name, lc in cfg.get("linters", {}).items()]

    if args.list_linters:
        for linter in linters:
            print(f"{linter.name}\t{linter.config.get('type')}")
        return 0

    if args.everything:
        files = subprocess.check_output(
            ["git", "ls-files"], cwd=REPO_ROOT, text=True).splitlines()
    elif args.paths:
        files = [_normalize_path(p) for p in args.paths]
    else:
        files = _changed_files()

    if not files:
        print("No files to lint.")
        return 0

    all_msgs: list[Message] = []
    for linter in linters:
        if args.only and not any(flt in linter.name for flt in args.only):
            continue
        runner = RUNNERS.get(linter.config.get("type"))
        if runner is None:
            continue
        matched = [f for f in files if linter.matches(f)]
        if not matched:
            continue
        try:
            all_msgs.extend(runner(linter, matched))
        except Exception as exc:  # noqa: BLE001
            print(f"[lint] {linter.name} failed: {exc}", file=sys.stderr)

    for m in all_msgs:
        print(m.format())

    errors = sum(1 for m in all_msgs if m.severity.lower() == "error")
    warnings = sum(1 for m in all_msgs if m.severity.lower() == "warning")
    print(f"\n{errors} error(s), {warnings} warning(s), {len(all_msgs)} total message(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
