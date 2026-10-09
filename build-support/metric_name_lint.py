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
"""metric-name-lint: keep dimensions out of metric names.

A metric name says *what is measured*. Anything that varies (RPC method, statement type, thread
pool, task type, table, command, enum value...) belongs in a label. See METRIC_NAMING.md.

The gate is a ratchet: findings already recorded in the baseline file are reported but do not
fail; anything new fails. Baseline entries that no longer match are reported so the baseline can
only shrink (`--update-baseline` rewrites it).

Zero dependencies (stdlib only, Python 3.8+). Usage:

  metric_name_lint.py                      # scan repo per .metric-name-lint.json, ratchet
  metric_name_lint.py --files a.cc b.go    # scan only these files (pre-commit / arc lint)
  metric_name_lint.py --github             # also emit GitHub Actions annotations
  metric_name_lint.py --update-baseline    # accept current findings as the new baseline
  metric_name_lint.py --scrape dump.prom   # check a live /prometheus-metrics scrape

Inline suppression (reason required), on the offending line or the line above:
  // metric-name-lint: allow(<reason>)      # also '#' comments
"""

import argparse
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

VERSION = "1.0"
CONFIG_FILE = ".metric-name-lint.json"
DOC = "METRIC_NAMING.md"

DEFAULT_CONFIG = {
    "baseline": "metric_name_lint_baseline.json",
    "include": ["**/*.cc", "**/*.h", "**/*.c", "**/*.go", "**/*.java", "**/*.py",
                "**/*.yml", "**/*.yaml", "**/*.json", "**/*.tf", "**/*.tmpl"],
    "exclude": ["**/node_modules/**", "**/vendor/**", "**/third_party/**", "**/thirdparty/**",
                "**/build/**", "**/.git/**", "**/*test*/**", "**/*_test.*", "**/*-test.*",
                "**/test_*.py", "**/*Test.java", "**/*.min.js"],
    # Rules whose findings never fail the build (reported as warnings).
    "warn_only_rules": ["PROMQL_NAME_REGEX"],
    # Rules to skip entirely.
    "disabled_rules": [],
    # Families: N+ statically defined names sharing a prefix with value-like suffixes.
    "family_min_size": 3,
    "max_file_bytes": 2000000,
    # Where the naming standard lives in this repo (printed in every report).
    "guide": "METRIC_NAMING.md",
}

SUPPRESS_RX = re.compile(r"metric-name-lint:\s*allow\(\s*([^)\s][^)]*?)\s*\)")

RULES = {
    "DYNAMIC_NAME":
        "metric name is built at runtime from a variable; put the varying part in a label "
        "(MetricEntity attribute / label name) and keep the name constant",
    "TOKEN_PASTED_NAME":
        "macro/codegen pastes a value into a metric name; define one metric and pass the value "
        "as a label",
    "NAME_FAMILY":
        "these metrics differ only by a value-like suffix, i.e. a dimension encoded in the name; "
        "collapse into one metric with a label",
    "EMBEDDED_NAMESPACE":
        "metric name embeds an RPC namespace/service (e.g. _yb_tserver_); use "
        "server_type/service_type/service_method labels",
    "PREFIX_CONCAT":
        "string prefix is concatenated with a value to form a metric name; use a label",
    "PROMQL_NAME_REGEX":
        "query regex-matches __name__ to recover a dimension from metric names; this is a "
        "symptom of name-encoded dimensions (prefer a label selector)",
    "SCRAPE_FAMILY":
        "live scrape shows a family of names differing only by a value-like suffix",
}

# --------------------------------------------------------------------------------------------
# Small helpers


def is_str_literal(expr):
    e = expr.strip()
    if re.fullmatch(r'(u8|L|R)?"(?:[^"\\]|\\.)*"', e):
        return True
    if re.fullmatch(r"'(?:[^'\\]|\\.)*'", e):          # python single-quoted
        return True
    if re.fullmatch(r"#\s*\w+", e):                      # C preprocessor stringize (#name)
        return True
    if re.fullmatch(r"BOOST_PP_STRINGIZE\(\s*\w+\s*\)", e):
        return True
    return False


def is_constant_ident(expr):
    """UPPER_CASE / kConstant identifiers are treated as fixed names."""
    e = expr.strip()
    return bool(re.fullmatch(r"[A-Z][A-Z0-9_]*(\.[A-Z][A-Z0-9_]*)*", e) or
                re.fullmatch(r"k[A-Z]\w*", e) or
                re.fullmatch(r"(\w+\.)*[A-Z][A-Z0-9_]+", e))


def split_args(text, start):
    """Given text and index just after '(', return (list of top-level args, end index)."""
    depth, i, cur, args = 0, start, [], []
    in_str = None
    while i < len(text):
        c = text[i]
        if in_str:
            cur.append(c)
            if c == "\\":
                cur.append(text[i + 1:i + 2])
                i += 2
                continue
            if c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            if depth == 0 and c == ")":
                args.append("".join(cur).strip())
                return args, i
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
        if i - start > 4000:
            break
    return args, i


def line_of(text, idx):
    return text.count("\n", 0, idx) + 1


def strip_comments_keep_layout(text, lang):
    """Blank out comments (keeping newlines/offsets) so commented code does not match."""
    if lang in ("c", "go", "java"):
        def repl(m):
            return re.sub(r"[^\n]", " ", m.group(0))
        pattern = r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\''
        return re.sub(pattern, lambda m: m.group(0) if m.group(0)[0] in "\"'" else repl(m),
                      text, flags=re.S)
    if lang == "py":
        return re.sub(r"(?m)^\s*#[^\n]*", lambda m: " " * len(m.group(0)), text)
    return text


def lang_of(path):
    ext = os.path.splitext(path)[1].lower()
    return {".cc": "c", ".h": "c", ".c": "c", ".cpp": "c", ".hpp": "c", ".go": "go",
            ".java": "java", ".py": "py"}.get(ext, "cfg")


class Finding:
    __slots__ = ("rule", "path", "line", "key", "detail")

    def __init__(self, rule, path, line, key, detail):
        self.rule, self.path, self.line, self.key, self.detail = rule, path, line, key, detail

    @property
    def fingerprint(self):
        # Line numbers are deliberately excluded so unrelated edits don't churn the baseline.
        raw = "%s|%s|%s" % (self.rule, self.path, re.sub(r"\s+", "", self.key))
        return hashlib.sha1(raw.encode()).hexdigest()[:16]

    def message(self):
        return "%s: %s [%s]" % (self.detail, RULES[self.rule], self.rule)


# --------------------------------------------------------------------------------------------
# C / C++ rules (yugabyte-db server code, YSQL extension)

CPP_OWNING = re.compile(
    r"\bOwning(?:Gauge|Counter|Histogram|EventStats|Lag|Metric)?Prototype"
    r"\s*(?:<[^;(]*?>)?\s*>?\s*\(")
CPP_CTORARGS = re.compile(r"\bMetricPrototype::CtorArgs\s*\(")
CPP_DEFINE = re.compile(r"\bMETRIC_DEFINE_(?!entity\b)[a-z_0-9]+\s*\(\s*([A-Za-z_]\w*)\s*,\s*"
                        r"([A-Za-z_]\w*)\s*,")
CPP_PASTE_DEFINE = re.compile(r"(?m)^[ \t]*#[ \t]*define[ \t]+\w+(?:\([^)]*\))?(?:[^\n]*\\\n)*"
                              r"[^\n]*")
CPP_PREFIX_DEFINE = re.compile(
    r'(?m)^[ \t]*#[ \t]*define[ \t]+(\w*(?:METRIC|LATENCY|STAT)\w*PREFIX\w*)'
    r'[ \t]+"([A-Za-z0-9_]+_)"')
CPP_PASTED_DEFINE_ARG = re.compile(r"\bMETRIC_DEFINE_[a-z_0-9]+[\s\\]*\([\s\\]*\w+[\s\\]*,[\s\\]*"
                                   r"(BOOST_PP_CAT\s*\(|\w+\s*##|##)")


def _name_origin(raw, pos, ident):
    """Look back from a prototype ctor for `ident = Format("...")` to show the name pattern."""
    ident = re.sub(r"^std::move\((\w+)\)$", r"\1", ident.strip())
    if not re.fullmatch(r"\w+", ident):
        return None
    window = raw[max(0, pos - 2000):pos]
    ms = list(re.finditer(r"\b%s\s*(?:=|\()\s*(?:yb::)?(?:Format|StrCat|Substitute)\s*\(\s*"
                          r"\"([^\"]+)\"" % re.escape(ident), window))
    if ms:
        return ms[-1].group(1)
    ms = list(re.finditer(r"\b%s\s*=\s*(\"[^\"]*\"\s*\+[^;]+|[^;]*\+\s*\"[^\"]*\"[^;]*);"
                          % re.escape(ident), window))
    return _short(ms[-1].group(1), 60) if ms else None


def scan_c(path, text, raw, cfg):
    out = []
    # DYNAMIC_NAME: Owning*Prototype(entity, name, ...) with non-literal name.
    for rx, name_idx in ((CPP_OWNING, 1), (CPP_CTORARGS, 1)):
        for m in rx.finditer(text):
            args, _ = split_args(raw, m.end())
            if len(args) <= name_idx:
                continue
            name = args[name_idx]
            if is_str_literal(name) or is_constant_ident(name):
                continue
            origin = _name_origin(raw, m.start(), name)
            detail = "metric prototype name `%s`" % _short(name)
            if origin:
                detail += " built as `%s`" % origin
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "proto:" + (origin or name), detail))
    # TOKEN_PASTED_NAME: #define macros whose METRIC_DEFINE_* name argument is token-pasted,
    # e.g. METRIC_DEFINE_x(entity, BOOST_PP_CAT(prefix_, name), ...).
    for m in CPP_PASTE_DEFINE.finditer(text):
        body = m.group(0)
        if CPP_PASTED_DEFINE_ARG.search(body):
            head = re.match(r"\s*#\s*define\s+(\w+)", body).group(1)
            out.append(Finding("TOKEN_PASTED_NAME", path, line_of(text, m.start()),
                               "macro:" + head, "macro `%s`" % head))
    # TOKEN_PASTED_NAME: code generators emitting METRIC_DEFINE with $substitutions$.
    for m in re.finditer(r'"[^"\n]*(?<!METRIC_)\$metric_prefix\$\$metric_name\$_\$[a-z_]+\$'
                         r'[^"\n]*"', raw):
        out.append(Finding("TOKEN_PASTED_NAME", path, line_of(raw, m.start()),
                           "codegen:" + m.group(0), "generated metric name template %s"
                           % _short(m.group(0))))
    # PREFIX_CONCAT: #define FOO_METRIC_PREFIX "handler_latency_..._" (C, YSQL extension).
    for m in CPP_PREFIX_DEFINE.finditer(raw):
        out.append(Finding("PREFIX_CONCAT", path, line_of(raw, m.start()),
                           "prefix:" + m.group(1), "`%s` = \"%s\"" % (m.group(1), m.group(2))))
    return out


def collect_cpp_defines(path, text):
    return [(m.group(2), m.group(1), path, line_of(text, m.start()))
            for m in CPP_DEFINE.finditer(text)]


VALUE_WORDS = {"read", "write", "local", "remote", "insert", "update", "delete", "select",
               "get", "put", "set", "add", "remove", "create", "drop", "alter", "master",
               "tserver", "leader", "follower", "success", "failure", "failed", "ok", "error"}


def _value_like(suffix):
    first = suffix.split("_")[0]
    # Deliberately narrow: CamelCase tokens (RPC services/methods, statement types, commands)
    # and bare numbers. Lowercase suffixes are too often distinct quantities (read_us/add/...).
    return bool(first) and (first[0].isupper() or first.isdigit())


def family_findings(defs, cfg):
    """NAME_FAMILY: group statically defined names by longest shared prefix."""
    out, n_min = [], cfg["family_min_size"]
    by_prefix = defaultdict(list)
    for name, entity, path, line in defs:
        toks = name.split("_")
        for i in range(2, len(toks)):
            by_prefix["_".join(toks[:i]) + "_"].append((name, path, line))
    claimed = set()
    for prefix in sorted(by_prefix, key=len, reverse=True):     # longest prefix wins
        members = sorted({m for m in by_prefix[prefix] if m[0] not in claimed})
        vals = [m for m in members if _value_like(m[0][len(prefix):])]
        if len(vals) < n_min:
            continue
        sample = ", ".join(sorted(v[0][len(prefix):] for v in vals)[:6])
        family = "%d metrics `%s{%s%s}`" % (len(vals), prefix, sample,
                                            ", ..." if len(vals) > 6 else "")
        # One finding per member, keyed by member name: adding a member to a baselined family
        # is a new finding, and removing one shows up as a stale (shrinking) entry.
        for m in vals:
            claimed.add(m[0])
            out.append(Finding("NAME_FAMILY", m[1], m[2], "family:%s|%s" % (prefix, m[0]),
                               "`%s`, one of %s" % (m[0], family)))
    # EMBEDDED_NAMESPACE: handler_latency_yb_cqlserver_... style static names.
    for name, entity, path, line in defs:
        if name in claimed:
            continue
        if re.search(r"[a-z]_yb_[a-z]+_", name):
            out.append(Finding("EMBEDDED_NAMESPACE", path, line, "ns:" + name,
                               "metric `%s`" % name))
    return out


# --------------------------------------------------------------------------------------------
# Go (prometheus/client_golang)

GO_OPTS = re.compile(r"\b(?:prometheus\.)?(?:Counter|Gauge|Histogram|Summary|Untyped)"
                     r"(?:Vec)?Opts\s*\{")
GO_DESC = re.compile(r"\bprometheus\.NewDesc\s*\(")
GO_FQ = re.compile(r"\bprometheus\.BuildFQName\s*\(")


def _dynamic_expr(expr):
    e = expr.strip()
    if is_str_literal(e) or is_constant_ident(e):
        return False
    return bool(re.search(r"Sprintf|\+|Join|format\(|\.formatted\(|String\.format|f[\"']|"
                          r"StrCat|Format\(", e)) or bool(re.fullmatch(r"[a-z]\w*(\.\w+)*", e))


def _assigned_dynamic(raw, pos, ident):
    """For a bare identifier used as a metric name, look back for `ident = <dynamic expr>`
    (Java `String n = "a_" + x;`, Go `n := fmt.Sprintf(...)`, Python `n = f"..."`). Returns the
    assigned expression when it builds the name at runtime, else None. Parameters such as the
    `name` in a `buildCounter(String name, ...)` wrapper have no assignment and are not
    flagged."""
    ident = ident.strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", ident):
        return None
    window = raw[max(0, pos - 2000):pos]
    ms = list(re.finditer(r"\b%s\s*(?::=|=)(?!=)\s*([^\n;]+)" % re.escape(ident), window))
    if not ms:
        return None
    rhs = ms[-1].group(1).strip()
    if re.fullmatch(r"[A-Za-z_]\w*", rhs) or not _dynamic_expr(rhs):
        return None
    return rhs


def _name_expr_dynamic(raw, pos, expr):
    """Detail string if `expr` (a metric-name argument) is built at runtime, else None."""
    e = expr.strip()
    if re.fullmatch(r"\w+", e):
        rhs = _assigned_dynamic(raw, pos, e)
        return "`%s` = `%s`" % (e, _short(rhs, 50)) if rhs else None
    return "`%s`" % _short(e) if _dynamic_expr(e) else None


def scan_go(path, text, raw, cfg):
    out = []
    for m in GO_OPTS.finditer(text):
        body = raw[m.end():m.end() + 800]
        end = body.find("}")
        body = body[:end if end >= 0 else 800]
        nm = re.search(r"\bName:\s*([^,\n]+)", body)
        dyn = nm and _name_expr_dynamic(raw, m.start(), nm.group(1))
        if dyn:
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "goopts:" + nm.group(1), "Opts.Name %s (use Namespace/"
                               "Subsystem + constant Name, labels for values)" % dyn))
    for m in GO_DESC.finditer(text):
        args, _ = split_args(raw, m.end())
        if args and _dynamic_expr(args[0]) and not args[0].startswith("prometheus.BuildFQName"):
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "godesc:" + args[0], "NewDesc name `%s`" % _short(args[0])))
    for m in GO_FQ.finditer(text):
        args, _ = split_args(raw, m.end())
        if len(args) == 3 and _dynamic_expr(args[2]):
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "gofq:" + args[2], "BuildFQName name part `%s`"
                               % _short(args[2])))
    return out


# --------------------------------------------------------------------------------------------
# Java (io.prometheus simpleclient / client_java 1.x, Micrometer, local build* wrappers)

JAVA_BUILDER = re.compile(r"\b(?:Counter|Gauge|Summary|Histogram|Info|StateSet)\s*\.\s*"
                          r"(?:builder|build)\s*\(")
JAVA_WRAPPER = re.compile(r"\bbuild(?:Counter|Gauge|Summary|Histogram)\s*\(")
JAVA_METRICDEF = re.compile(r"\bnew\s+MetricDefinition\s*\(")
JAVA_MICROMETER = re.compile(r"\.(?:counter|gauge|timer|summary)\s*\(\s*(\"[^\"]*\"\s*\+|"
                             r"String\.format\s*\()")


def _java_decl(args):
    return args and re.fullmatch(r"(final\s+)?String\s+\w+", args[0].strip())


def scan_java(path, text, raw, cfg):
    out = []
    for m in JAVA_BUILDER.finditer(text):
        args, end = split_args(raw, m.end())
        cands = []
        if args and args[0]:
            cands.append(args[0])                              # simpleclient build(name, help)
        chain = raw[end + 1:end + 400]
        nm = re.search(r"^\s*(?:\.\s*\w+\s*\([^;]*?\)\s*)*?\.\s*name\s*\(", chain, re.S)
        if nm:
            a2, _ = split_args(chain, nm.end())
            if a2:
                cands.append(a2[0])
        for c in cands:
            dyn = _name_expr_dynamic(raw, m.start(), c)
            if dyn:
                out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                                   "javab:" + c, "metric builder name %s" % dyn))
    for m in JAVA_WRAPPER.finditer(text):
        args, _ = split_args(raw, m.end())
        if not args or _java_decl(args):
            continue
        dyn = _name_expr_dynamic(raw, m.start(), args[0])
        if dyn:
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "javaw:" + args[0], "metric name %s" % dyn))
    # yugabyte-cloud style: new MetricDefinition(name, description, tags) handed to Micrometer.
    for m in JAVA_METRICDEF.finditer(text):
        args, _ = split_args(raw, m.end())
        dyn = args and _name_expr_dynamic(raw, m.start(), args[0])
        if dyn and not re.search(r"\.getName\(\)$", args[0].strip()):
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "javad:" + args[0], "MetricDefinition name %s" % dyn))
    for m in (JAVA_MICROMETER.finditer(text) if "io.micrometer" in raw else ()):
        out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                           "javam:" + raw[m.start():m.end() + 60].split("\n")[0],
                           "Micrometer meter name built by concatenation"))
    return out


# --------------------------------------------------------------------------------------------
# Python (prometheus_client)

PY_CTOR = re.compile(r"\b(?:prometheus_client\.)?(?:Gauge|Counter|Histogram|Summary|Info|Enum)"
                     r"\s*\(")


def scan_py(path, text, raw, cfg):
    if "prometheus_client" not in raw:
        return []
    out = []
    for m in PY_CTOR.finditer(text):
        args, _ = split_args(raw, m.end())
        if not args:
            continue
        a0 = args[0]
        if re.match(r"(name|documentation)\s*=", a0):
            kw = [a for a in args if a.startswith("name")]
            a0 = kw[0].split("=", 1)[1] if kw else ""
        concat = re.search(r"[\"']\s*\+|\+\s*[\"']|\.format\(|%", a0 or "")
        if a0 and (re.match(r"\s*f[\"']", a0) or concat):
            out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                               "py:" + a0, "prometheus_client name `%s`" % _short(a0)))
        elif a0 and re.fullmatch(r"\w+", a0.strip()):
            rhs = _assigned_dynamic(raw, m.start(), a0)
            if rhs:
                out.append(Finding("DYNAMIC_NAME", path, line_of(text, m.start()),
                                   "py:" + a0, "prometheus_client name `%s` = `%s`"
                                   % (a0.strip(), _short(rhs, 50))))
    return out


# --------------------------------------------------------------------------------------------
# Consumers: PromQL in dashboards, alerts, configs, code

PROMQL_RX = re.compile(r"__name__\s*=~\s*\\*[\"']((?:[^\"'\\]|\\[^\"'])+)")


def _top_level_alts(rx):
    alts, depth, cur = [], 0, []
    for c in rx:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "|" and depth == 0:
            alts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    alts.append("".join(cur))
    return alts


def scan_promql(path, text, raw, cfg):
    out = []
    for m in PROMQL_RX.finditer(raw):
        rx = m.group(1)
        # Only flag regexes that select a family of YB-style names (prefix + wildcard/alternation
        # on a value), not plain allow/deny lists of unrelated metrics.
        alts = [a for a in _top_level_alts(rx) if a]
        one_family = len(alts) == 1 or len(os.path.commonprefix(alts)) >= 4
        if one_family and (re.search(r"_\.?[*+]|_\(|_\[", rx) or len(alts) > 1):
            out.append(Finding("PROMQL_NAME_REGEX", path, line_of(raw, m.start()),
                               "promql:" + rx, "`__name__=~\"%s\"`" % _short(rx)))
    return out


# --------------------------------------------------------------------------------------------
# Live scrape mode


def scan_scrape(path, cfg):
    names = set()
    with open(path, errors="ignore") as f:
        for line in f:
            if not line or line[0] == "#":
                continue
            m = re.match(r"([a-zA-Z_:][a-zA-Z0-9_:]*)", line)
            if m:
                n = re.sub(r"_(sum|count|bucket|total|created)$", "", m.group(1))
                names.add(n)
    # Constant pseudo-path so fingerprints don't depend on the dump's file name.
    defs = [(n, "scrape", "<scrape>", 0) for n in sorted(names)]
    return [Finding("SCRAPE_FAMILY", f.path, 0, f.key, f.detail)
            for f in family_findings(defs, cfg) if f.rule == "NAME_FAMILY"]


# --------------------------------------------------------------------------------------------
# Driver


def _short(s, n=70):
    s = re.sub(r"\s+", " ", s.strip())
    return s if len(s) <= n else s[:n - 3] + "..."


def repo_root():
    try:
        return subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except Exception:
        return os.getcwd()


def load_config(root):
    cfg = dict(DEFAULT_CONFIG)
    p = os.path.join(root, CONFIG_FILE)
    if os.path.exists(p):
        with open(p) as f:
            cfg.update(json.load(f))
    return cfg


def _match(path, globs):
    for g in globs:
        # fnmatch's '*' already crosses '/', so '**/' only needs to also match "no directory".
        for pat in {g, g.replace("**/", "")}:
            if fnmatch.fnmatch(path, pat) or fnmatch.fnmatch("/" + path, pat):
                return True
    return False


def list_files(root, cfg):
    try:
        files = subprocess.check_output(["git", "ls-files"], cwd=root, text=True,
                                        stderr=subprocess.DEVNULL).splitlines()
    except Exception:
        files = []
    if not files:
        for d, _, fs in os.walk(root):
            for f in fs:
                files.append(os.path.relpath(os.path.join(d, f), root))
    own = {os.path.normpath(cfg["baseline"]), CONFIG_FILE}
    return [f for f in files if _match(f, cfg["include"]) and not _match(f, cfg["exclude"])
            and os.path.normpath(f) not in own]


def suppressed(raw_lines, line):
    for ln in (line, line - 1):
        if 1 <= ln <= len(raw_lines) and SUPPRESS_RX.search(raw_lines[ln - 1]):
            return True
    return False


def scan(root, files, cfg):
    findings, defs = [], []
    for rel in files:
        p = os.path.join(root, rel)
        try:
            if os.path.getsize(p) > cfg["max_file_bytes"]:
                continue
            with open(p, errors="ignore") as f:
                raw = f.read()
        except OSError:
            continue
        lang = lang_of(rel)
        text = strip_comments_keep_layout(raw, lang)
        got = []
        if lang == "c":
            got += scan_c(rel, text, raw, cfg)
            defs += collect_cpp_defines(rel, text)
        elif lang == "go":
            got += scan_go(rel, text, raw, cfg)
        elif lang == "java":
            got += scan_java(rel, text, raw, cfg)
        elif lang == "py":
            got += scan_py(rel, text, raw, cfg)
        got += scan_promql(rel, text, raw, cfg)
        lines = raw.split("\n")
        findings += [f for f in got if not suppressed(lines, f.line)]
        _lines_cache[rel] = lines
    for f in family_findings(defs, cfg):
        if not suppressed(_lines_cache.get(f.path, []), f.line):
            findings.append(f)
    disabled = set(cfg["disabled_rules"])
    uniq = {}
    for f in findings:
        if f.rule not in disabled:
            uniq.setdefault(f.fingerprint, f)
    return sorted(uniq.values(), key=lambda f: (f.path, f.line, f.rule))


_lines_cache = {}


def load_baseline(path):
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        data = json.load(f)
    return {e["fingerprint"]: e for e in data.get("findings", [])}


def write_baseline(path, findings):
    data = {
        "_comment": "Known metric-naming debt. Generated by metric_name_lint.py "
                    "--update-baseline. This list may only shrink; see " + DOC + ".",
        "version": VERSION,
        "findings": [{"fingerprint": f.fingerprint, "rule": f.rule, "path": f.path,
                      "detail": f.detail} for f in findings],
    }
    with open(path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=False)
        f.write("\n")


def _git_show(root, ref, rel):
    """Contents of ref:rel, or None if the path does not exist at ref. Any other git failure
    (unknown ref, unfetched base) raises, so it can't be mistaken for 'no baseline yet'."""
    try:
        subprocess.check_output(["git", "rev-parse", "--verify", "--quiet", ref + "^{commit}"],
                                cwd=root, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, OSError):
        raise RuntimeError("cannot resolve git ref %r for the baseline-growth check" % ref)
    p = subprocess.run(["git", "cat-file", "-e", "%s:%s" % (ref, rel)], cwd=root,
                       stderr=subprocess.DEVNULL)
    if p.returncode != 0:
        return None
    return subprocess.check_output(["git", "show", "%s:%s" % (ref, rel)], cwd=root, text=True,
                                   stderr=subprocess.DEVNULL)


GATE_KEYS = ("baseline", "include", "exclude", "warn_only_rules", "disabled_rules",
             "family_min_size", "max_file_bytes")


def baseline_growth(root, ref, cfg, baseline, baseline_override):
    """Entries the change adds to the baseline, relative to `ref`.

    Everything is judged by the *base* branch's config, so a change can't dodge the check by
    pointing `baseline` at a new file or by loosening include/exclude/rules: any change to the
    gate settings counts as growth and needs the same approved exception."""
    old_cfg_raw = _git_show(root, ref, CONFIG_FILE)
    if old_cfg_raw is None and os.path.exists(os.path.join(root, CONFIG_FILE)):
        return []                                   # the gate itself is introduced by this change
    old_cfg = dict(DEFAULT_CONFIG)
    if old_cfg_raw is not None:
        try:
            old_cfg.update(json.loads(old_cfg_raw))
        except ValueError:
            raise RuntimeError("%s at %s is not valid JSON" % (CONFIG_FILE, ref))
    grown = []
    changed = [k for k in GATE_KEYS if old_cfg.get(k) != cfg.get(k)]
    if changed:
        grown.append({"rule": "CONFIG", "path": CONFIG_FILE,
                      "detail": "gate settings changed: %s" % ", ".join(changed)})
    rel = baseline_override or old_cfg["baseline"]
    old_raw = _git_show(root, ref, rel)
    try:
        old = json.loads(old_raw) if old_raw is not None else {}
    except ValueError:
        raise RuntimeError("baseline %s at %s is not valid JSON" % (rel, ref))
    old_fps = {e["fingerprint"] for e in old.get("findings", [])}
    old_warn = set(old_cfg["warn_only_rules"])
    grown += [e for fp, e in baseline.items()
              if fp not in old_fps and e.get("rule") not in old_warn]
    return grown


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--files", nargs="*", help="only scan these files (repo-relative)")
    ap.add_argument("--root", help="repo root (default: git toplevel)")
    ap.add_argument("--baseline", help="baseline file (default from config)")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--no-baseline", action="store_true", help="report everything as new")
    ap.add_argument("--github", action="store_true", help="emit GitHub annotations")
    ap.add_argument("--format", choices=["text", "arc", "json"], default="text")
    ap.add_argument("--scrape", metavar="FILE", help="check a Prometheus text exposition dump")
    ap.add_argument("--strict", action="store_true", help="warn-only rules also fail")
    ap.add_argument("--no-baseline-growth", metavar="GIT_REF",
                    help="fail if the baseline gained entries relative to GIT_REF (e.g. "
                         "origin/master); set METRIC_NAME_LINT_ALLOW_GROWTH=1 to override")
    ap.add_argument("--fail-on-stale", action="store_true",
                    help="fail if baseline has fixed entries (forces the baseline to shrink)")
    ap.add_argument("--arc-severity", choices=["error", "warning"], default="error",
                    help="severity of blocking findings in --format arc (warning keeps the "
                         "per-file lint advisory)")
    args = ap.parse_args(argv)

    root = os.path.abspath(args.root or repo_root())
    cfg = load_config(root)
    global DOC
    DOC = cfg["guide"]
    base_path = os.path.join(root, args.baseline or cfg["baseline"])

    if args.scrape:
        findings = scan_scrape(args.scrape, cfg)
        scope_full = False
    else:
        if args.files is not None:
            files = [os.path.relpath(os.path.abspath(f), root) if os.path.isabs(f) else f
                     for f in args.files]
            files = [f for f in files if _match(f, cfg["include"]) and
                     not _match(f, cfg["exclude"])]
        else:
            files = list_files(root, cfg)
        findings = scan(root, files, cfg)
        scope_full = args.files is None

    if args.update_baseline:
        # A partial scan would silently drop every entry outside its scope.
        if args.files is not None:
            print("metric-name-lint: --update-baseline needs a full scan; drop --files",
                  file=sys.stderr)
            return 2
        if args.scrape and not args.baseline:
            print("metric-name-lint: --update-baseline with --scrape needs an explicit "
                  "--baseline (the scrape baseline is a separate file)", file=sys.stderr)
            return 2
        write_baseline(base_path, findings)
        print("metric-name-lint: wrote %d findings to %s" % (
            len(findings), os.path.relpath(base_path, root)))
        return 0

    baseline = {} if args.no_baseline else load_baseline(base_path)
    warn_only = set() if args.strict else set(cfg["warn_only_rules"])

    grown = []
    if args.no_baseline_growth:
        try:
            grown = baseline_growth(root, args.no_baseline_growth, cfg, baseline,
                                    args.baseline)
        except RuntimeError as e:
            print("metric-name-lint: %s" % e, file=sys.stderr)
            return 2
        if grown and os.environ.get("METRIC_NAME_LINT_ALLOW_GROWTH") == "1":
            print("metric-name-lint: baseline grew by %d entr%s (override approved)" %
                  (len(grown), "y" if len(grown) == 1 else "ies"))
            grown = []
    new = [f for f in findings if f.fingerprint not in baseline]
    known = [f for f in findings if f.fingerprint in baseline]
    blocking = [f for f in new if f.rule not in warn_only]
    stale = []
    if scope_full:
        seen = {f.fingerprint for f in findings}
        stale = [e for fp, e in baseline.items() if fp not in seen]
    # Warn-only findings (PromQL) never block, so a fixed one doesn't force a baseline update.
    stale_blocking = [e for e in stale if e["rule"] not in warn_only]

    if args.format == "json":
        print(json.dumps({
            "new": [dict(rule=f.rule, path=f.path, line=f.line, message=f.message(),
                         fingerprint=f.fingerprint, blocking=f.rule not in warn_only)
                    for f in new],
            "baselined": len(known), "stale_baseline": stale}, indent=2))
    elif args.format == "arc":
        # One line per finding, consumed by yugabyte-db build-support/lint.py script-and-regex.
        for f in new:
            sev = args.arc_severity if f.rule not in warn_only else "warning"
            print("%s:%d:%s:%s:%s" % (f.path, f.line, sev, f.rule, f.message()))
    else:
        for f in new:
            tag = "ERROR" if f.rule not in warn_only else "warn "
            print("%s %s:%d: %s" % (tag, f.path, f.line, f.message()))
        if stale:
            print("\nmetric-name-lint: %d baseline entr%s no longer found (fixed? run "
                  "--update-baseline to shrink the baseline):" %
                  (len(stale), "y" if len(stale) == 1 else "ies"))
            for e in stale:
                print("  - %s %s: %s" % (e["rule"], e["path"], e["detail"]))
        if grown:
            print("\nmetric-name-lint: this change ADDS %d entr%s to the baseline. New naming "
                  "debt needs an approved exception (see %s):" %
                  (len(grown), "y" if len(grown) == 1 else "ies", DOC))
            for e in grown:
                print("  + %s %s: %s" % (e["rule"], e["path"], e["detail"]))
        print("\nmetric-name-lint: %d new (%d blocking), %d baselined, %d stale. Guide: %s" %
              (len(new), len(blocking), len(known), len(stale), DOC))

    if args.github:
        for f in new:
            lvl = "error" if f.rule not in warn_only else "warning"
            msg = f.message().replace("%", "%25").replace("\n", "%0A")
            print("::%s file=%s,line=%d,title=metric-name-lint %s::%s" %
                  (lvl, f.path, max(f.line, 1), f.rule, msg))
        if stale:
            print("::warning title=metric-name-lint::%d baseline entries are fixed; run "
                  "metric_name_lint.py --update-baseline to shrink the baseline" % len(stale))

    if args.github and grown:
        print("::error title=metric-name-lint::baseline grew by %d entries; new metric-naming "
              "debt needs the 'metric-naming-exception' label" % len(grown))
    return 1 if blocking or grown or (args.fail_on_stale and stale_blocking) else 0


if __name__ == "__main__":
    sys.exit(main())
