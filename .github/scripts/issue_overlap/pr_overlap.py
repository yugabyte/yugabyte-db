#!/usr/bin/env python3
"""Find open yugabyte-db PRs that change the same code as a given PR.

Two PRs overlap when they change the same function in the same file, or when
their changed lines share an identifier that few other open PRs touch (for
example `shared_simple_eval_resowner`). Sharing only a file is shown as context
but is not enough to flag a pair: hot files make that too noisy.

Usage:
  pr_overlap.py 38804                 # PRs overlapping one PR (any state)
  pr_overlap.py --terms shared_simple_eval_resowner pl_exec.c
                                      # open PRs whose diff mentions these
  pr_overlap.py --all                 # every overlapping pair in the pool

The pool is open PRs against master, pg19 and feature-stack/* branches,
without backports. Per-PR file data is cached by head SHA in .cache/.
"""
import argparse
import concurrent.futures
import json
import math
import os
import re
import subprocess
import sys
from collections import defaultdict

REPO = "yugabyte/yugabyte-db"
CACHE = os.environ.get("PR_OVERLAP_CACHE") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), ".cache")
DEFAULT_BASES = re.compile(r"^(master|pg19|feature-stack/.*)$")

# Paths whose overlap says nothing about intent.
IGNORED_PATHS = re.compile(
    r"(yb_typedefs\.list$|/expected/|\.lock$|package-lock\.json$|go\.sum$)")

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]{5,}")
# snake_case or CamelCase; plain English words in comments ("attempted",
# "Expressions") matched too many unrelated PRs.
CODE_LIKE = re.compile(r"_|[a-z][A-Z]|[A-Z]{2}[a-z]")
FUNC_DEF = re.compile(r"^[ +-]((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\(")
HUNK = re.compile(r"^@@ [^@]* @@ ?(.*)$")
# Only source files have meaningful function names in hunk headers; CMake,
# sbt and Go import blocks gave "set", "in", "import".
CODE_FILE = re.compile(r"\.(c|cc|cpp|h|hpp|java|py|go|ts|tsx|js|scala|rs)$")
# Test and flag macros (TEST_F, DECLARE_bool, METRIC_DEFINE_entity) aren't
# functions two PRs could clash in.
MACRO = re.compile(r"^(TEST|DECLARE|DEFINE|METRIC|GTEST|INSTANTIATE)|^[A-Z0-9_]+$")
NOT_FUNC = set("if for while switch return sizeof set in const import "
               "func var let new delete catch elif else".split())
CALL = re.compile(r"((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*\(")
STOP = set("""
return static struct extern const unsigned signed sizeof typedef switch
default continue include define ifdef ifndef endif pragma nullptr
public private protected virtual override template typename namespace
Status Result string vector size_t int32_t int64_t uint32_t uint64_t
assert Assert elog ereport errmsg errcode errdetail errhint ERROR WARNING
LOG DEBUG1 NOTICE PG_TRY PG_CATCH PG_END_TRY PG_FINALLY palloc pfree
IsYugaByteEnabled RETURN_NOT_OK ASSERT_OK ASSERT_EQ EXPECT_EQ ASSERT_TRUE
EXPECT_TRUE ASSERT_FALSE EXPECT_FALSE LOG_WITH_PREFIX VLOG_WITH_PREFIX
""".split())


def gh(*args):
    out = subprocess.run(["gh", *args], check=True, capture_output=True,
                         text=True).stdout
    return out


def pool_prs(bases):
    prs = json.loads(gh("pr", "list", "--repo", REPO, "--state", "open",
                        "--limit", "3000", "--json",
                        "number,title,author,baseRefName,headRefOid,url,"
                        "body"))
    return [p for p in prs if bases.match(p["baseRefName"])
            and not re.match(r"^\s*\[BACKPORT", p["title"], re.I)]


def pr_meta(num):
    return json.loads(gh("pr", "view", str(num), "--repo", REPO, "--json",
                         "number,title,author,baseRefName,headRefOid,url,"
                         "state"))


def fetch_files(pr):
    """List of {path, patch} for a PR, cached by head SHA."""
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{pr['number']}-{pr['headRefOid']}.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    raw = gh("api", "--paginate", f"repos/{REPO}/pulls/{pr['number']}/files",
             "--jq", ".[] | {path: .filename, patch: (.patch // \"\")}")
    files = [json.loads(line) for line in raw.splitlines() if line.strip()]
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(files, f)
    os.replace(tmp, path)
    return files


def summarize(files):
    """Files, (path, function) pairs and identifiers a PR's diff changes."""
    paths, funcs, idents = set(), set(), set()
    text = []
    for f in files:
        p = f["path"]
        if IGNORED_PATHS.search(p):
            continue
        paths.add(p)
        is_code = bool(CODE_FILE.search(p))

        def add_func(name):
            name = name.split("::")[-1]
            if (is_code and len(name) >= 4 and name not in NOT_FUNC
                    and not MACRO.match(name)):
                funcs.add((p, name))

        for line in f["patch"].splitlines():
            m = HUNK.match(line)
            if m:
                c = CALL.search(m.group(1))
                if c:
                    add_func(c.group(1))
                continue
            m = FUNC_DEF.match(line)
            if m:
                add_func(m.group(1))
            if line[:1] in "+-" and not line.startswith(("+++", "---")):
                text.append(line[1:])
                idents.update(t for t in IDENT.findall(line[1:])
                              if t not in STOP and CODE_LIKE.search(t))
    return {"paths": paths, "funcs": funcs, "idents": idents,
            "text": "\n".join(text)}


def load(prs, jobs=8):
    out = {}
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        futs = {ex.submit(fetch_files, p): p for p in prs}
        for i, fut in enumerate(concurrent.futures.as_completed(futs), 1):
            p = futs[fut]
            try:
                out[p["number"]] = summarize(fut.result())
            except subprocess.CalledProcessError as e:
                print(f"warning: #{p['number']}: {e.stderr.strip()}",
                      file=sys.stderr)
            if sys.stderr.isatty() and i % 25 == 0:
                print(f"  fetched {i}/{len(futs)}", file=sys.stderr)
    return out


def compare(a, b, df, max_df):
    funcs = sorted(f"{p}:{fn}" for p, fn in a["funcs"] & b["funcs"])
    rare = sorted(t for t in a["idents"] & b["idents"] if df[t] <= max_df)
    files = sorted(a["paths"] & b["paths"])
    score = 5 * len(funcs) + 2 * min(len(rare), 5) + min(len(files), 3)
    # One shared rare identifier alone (a GUC name in a test) was mostly noise.
    flagged = bool(funcs) or len(rare) >= 2
    return flagged, score, funcs, rare, files


def describe(meta):
    return (f"#{meta['number']} [{meta['baseRefName']}] {meta['title']} "
            f"(@{meta['author']['login']}) {meta['url']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("pr", nargs="?", type=int, help="PR to check")
    ap.add_argument("--terms", nargs="+",
                    help="identifiers or path fragments to look for")
    ap.add_argument("--all", action="store_true", help="report all pairs")
    ap.add_argument("--bases", default=DEFAULT_BASES.pattern,
                    help="regex for base branches in the pool")
    ap.add_argument("--max-df", type=int, default=0,
                    help="identifier counts as rare if at most this many pool "
                         "PRs change it (default: max(3, 1%% of pool))")
    ap.add_argument("--max-files", type=int, default=150,
                    help="--all skips PRs changing more files (default 150)")
    ap.add_argument("--json", action="store_true", help="JSON output")
    args = ap.parse_args()
    if sum(bool(x) for x in (args.pr, args.terms, args.all)) != 1:
        ap.error("give exactly one of PR, --terms, --all")

    pool = pool_prs(re.compile(args.bases))
    metas = {p["number"]: p for p in pool}
    target = None
    if args.pr:
        target = metas.get(args.pr) or pr_meta(args.pr)
        metas[args.pr] = target
    data = load(list(metas.values()))
    if target and target["number"] not in data:
        sys.exit(f"could not read #{target['number']}")

    if args.terms:
        hits = []
        for n, d in data.items():
            found = [t for t in args.terms
                     if t in d["text"] or any(t in p for p in d["paths"])]
            if found:
                hits.append((len(found), n, found))
        hits.sort(reverse=True)
        if args.json:
            print(json.dumps([{"pr": n, "terms": f} for _, n, f in hits]))
        for _, n, found in hits:
            print(f"{describe(metas[n])}\n    matches: {', '.join(found)}")
        if not hits:
            print("no open PR in the pool mentions these terms")
        return

    df = defaultdict(int)
    for n, d in data.items():
        if not target or n != target["number"] or target in pool:
            for t in d["idents"]:
                df[t] += 1
    max_df = args.max_df or max(3, math.ceil(len(pool) / 100))

    if args.pr:
        pairs = [(args.pr, n) for n in data if n != args.pr]
    else:
        # Catch-up merges and other huge PRs overlap everything; one author's
        # stacked PRs overlap on purpose. Neither is a surprise to anyone.
        big = {n for n, d in data.items() if len(d["paths"]) > args.max_files}
        if big:
            print(f"skipping {len(big)} PRs with > {args.max_files} files: "
                  f"{', '.join('#%d' % n for n in sorted(big))}",
                  file=sys.stderr)
        nums = sorted(n for n in data if n not in big)
        # Only compare PRs that share a file; keeps --all fast.
        by_path = defaultdict(set)
        for n in nums:
            for p in data[n]["paths"]:
                by_path[p].add(n)
        cand = set()
        for ns in by_path.values():
            if len(ns) <= 50:
                ns = sorted(ns)
                cand.update((a, b) for i, a in enumerate(ns) for b in ns[i+1:])
        author = {n: metas[n]["author"]["login"] for n in nums}
        pairs = sorted((a, b) for a, b in cand if author[a] != author[b])

    results = []
    for a, b in pairs:
        flagged, score, funcs, rare, files = compare(data[a], data[b], df,
                                                     max_df)
        if flagged:
            results.append({"a": a, "b": b, "score": score, "functions": funcs,
                            "identifiers": rare, "files": files})
    results.sort(key=lambda r: -r["score"])

    if args.json:
        print(json.dumps(results, indent=2))
        return
    if target:
        print(f"Checking {describe(target)}")
        print(f"against {len(pool)} open PRs (rare = changed by <= {max_df})\n")
    if not results:
        print("no overlapping PRs found")
    for r in results:
        other = r["b"] if target else None
        head = (describe(metas[other]) if target else
                f"{describe(metas[r['a']])}\n  <-> {describe(metas[r['b']])}")
        print(f"[score {r['score']}] {head}")
        if r["functions"]:
            print(f"    same functions:   {', '.join(r['functions'][:8])}")
        if r["identifiers"]:
            print(f"    rare identifiers: {', '.join(r['identifiers'][:8])}")
        if r["files"]:
            print(f"    same files:       {', '.join(r['files'][:5])}")
        print()


if __name__ == "__main__":
    main()
