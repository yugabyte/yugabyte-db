#!/usr/bin/env python3
"""Find work already in flight on a newly filed issue.

Run when an issue is created, before anyone is assigned, to find:
  - open PRs whose diff, title or body mention the issue's test names, error
    text or code identifiers (a fix may already be under way);
  - open GitHub issues and unresolved Jira DB issues that mention the same
    things (someone may already be investigating it).

Usage:
  issue_check.py --issue 38288        # a GitHub issue
  issue_check.py --jira DB-26593      # a Jira issue (reads the description,
                                      # which never syncs to GitHub)
  issue_check.py --text notes.txt     # any text; "-" reads stdin

Jira lookups need "email:token" in $JIRA_AUTH or ~/.claude/jira-api.token;
without either the Jira parts are skipped. Uses pr_overlap.py's cached PR diffs.
"""
import argparse
import base64
import json
import math
import os
import re
import sys
import time
import urllib.parse
import urllib.request

import pr_overlap as po

JIRA = "https://yugabyte.atlassian.net"
JIRA_TOKEN = os.path.expanduser("~/.claude/jira-api.token")
# "Github Public Description (Read-Only)": GitHub -> Jira one-way copy of the
# body of an issue filed on GitHub.
GH_DESC_FIELD = "customfield_10885"
# "GitHub issue link": URL of the issue's GitHub copy.
GH_LINK_FIELD = "customfield_10802"
MAX_SEARCH_TERMS = 6      # GitHub search allows 30 requests a minute
COMMON_HITS = 25          # a term in more open issues than this says nothing
MAX_TERM_LEN = 100        # GitHub search rejects queries over 256 characters
SEARCH_RETRY_WAIT = 61    # seconds; the search rate-limit window is a minute
MAX_ITEMS = 3             # per comment section
# An issue with this many terms (a long design-level report) matches PRs on
# shared vocabulary alone, so a PR needs one more shared term to be listed.
LONG_ISSUE_TERMS = 20

TEST_NAME = re.compile(
    r"\b(?:[A-Z]\w*Test\w*\.\w+|Test\w+#\w+|org\.yb\.[\w.]+#?\w*)")
ERROR_LINE = re.compile(r"(?:ERROR|FATAL|PANIC|Check failed):\s+(.+)")
URL = re.compile(r"https?://\S+")
# Parts of an error message that change from run to run: addresses, numbers
# and quoted names (relation "t1" does not exist).
VARIABLE = re.compile(r"\b0x[0-9a-f]+\b|\b\d+\b|\"[^\"]*\"|(?<!\w)'[^']*'")
# Lines the Jira sync adds to the GitHub copy of an issue.
JIRA_SYNC_LINE = re.compile(
    r"^(Jira Link:.*|\[[A-Z]+-\d+\]: https?://\S+)\r?$", re.M)


def jira_cred_source():
    return "$JIRA_AUTH" if os.environ.get("JIRA_AUTH") else JIRA_TOKEN


def jira_get(path, **q):
    cred = os.environ.get("JIRA_AUTH") or open(JIRA_TOKEN).read()
    auth = "Basic " + base64.b64encode(cred.strip().encode()).decode()
    url = f"{JIRA}/rest/api/3/{path}"
    if q:
        url += "?" + urllib.parse.urlencode(q)
    req = urllib.request.Request(
        url, headers={"Authorization": auth, "Accept": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=60))


def jira_post_comment(key, text):
    """Add a comment (Jira wiki markup) to a Jira issue."""
    cred = os.environ.get("JIRA_AUTH") or open(JIRA_TOKEN).read()
    req = urllib.request.Request(
        f"{JIRA}/rest/api/2/issue/{key}/comment",
        data=json.dumps({"body": text}).encode(), method="POST",
        headers={"Content-Type": "application/json",
                 "Authorization": "Basic " + base64.b64encode(
                     cred.strip().encode()).decode()})
    urllib.request.urlopen(req, timeout=60)


def adf_text(node):
    """Flatten Jira's Atlassian Document Format to plain text."""
    if isinstance(node, str):
        return node
    if isinstance(node, dict):
        out = node.get("text", "")
        sep = "\n" if node.get("type") in ("paragraph", "codeBlock",
                                            "heading", "listItem") else ""
        return out + "".join(adf_text(c) for c in node.get("content", [])) + sep
    if isinstance(node, list):
        return "".join(adf_text(c) for c in node)
    return ""


def load_issue(args):
    """(title, body, self-references to exclude)."""
    if args.issue:
        d = json.loads(po.gh("issue", "view", str(args.issue), "--repo",
                             po.REPO, "--json", "title,body"))
        title, body = d["title"], d["body"] or ""
        selfs = {f"#{args.issue}"}
        # The Jira issue the sync links to is this same issue.
        for line in JIRA_SYNC_LINE.findall(body):
            selfs.update(re.findall(r"\b[A-Z]+-\d+\b", line))
        body = JIRA_SYNC_LINE.sub("", body)
    elif args.jira:
        d = jira_get(f"issue/{args.jira}",
                     fields=f"summary,description,{GH_DESC_FIELD},{GH_LINK_FIELD}")
        title = d["fields"]["summary"]
        # An issue filed in Jira has its text in description; one filed on
        # GitHub has an empty description and its text in the synced field.
        body = "\n".join(adf_text(d["fields"].get(k) or {})
                         for k in ("description", GH_DESC_FIELD))
        selfs = {args.jira}
        # PRs name the GitHub copy ([#N]), so it is this issue too.
        m = re.search(r"/issues/(\d+)", d["fields"].get(GH_LINK_FIELD) or "")
        if m:
            selfs.add(f"#{m.group(1)}")
    else:
        body = sys.stdin.read() if args.text == "-" else open(args.text).read()
        title, selfs = "", set()
    return title, body, selfs


def extract_terms(title, body):
    """Strong terms (test names, error text) and code identifiers."""
    # Tokens inside links (Jira atlOrigin, %2F-encoded paths) are not terms.
    text = URL.sub(" ", f"{title}\n{body}")
    strong = set(TEST_NAME.findall(text))
    for m in ERROR_LINE.finditer(text):
        # Keep the error text up to the first address or number, which
        # changes from run to run.
        phrase = VARIABLE.split(m.group(1))[0].strip(" .:'\"`\\")
        if 15 <= len(phrase) <= MAX_TERM_LEN and '"' not in phrase:
            strong.add(phrase)
    idents = {t for t in po.IDENT.findall(text)
              if t not in po.STOP and po.CODE_LIKE.search(t)}
    idents -= {t for s in strong for t in po.IDENT.findall(s)}
    # A term inside another (pg_ddl_transaction in
    # pgwrapper_pg_ddl_transaction) would count one match twice.
    every = strong | idents
    idents = {t for t in idents if not any(t != o and t in o for o in every)}
    return strong, idents


def linked_pattern(selfs):
    """Regex for a reference to the issue (#N, issues/N, or its Jira key)."""
    alts = []
    for s in selfs:
        if s.startswith("#"):
            alts.append(rf"(?<![\w/])#{s[1:]}\b|/issues/{s[1:]}\b")
        else:
            alts.append(rf"\b{re.escape(s)}\b")
    return re.compile("|".join(alts)) if alts else None


def match_prs(strong, idents, data, metas, linked=None):
    """Score pool PRs by the issue terms their diff, title or body contain.

    Skips PRs whose title or body already reference the issue (linked):
    GitHub shows those on the issue already, and they were most of the
    comments in a sample of new issues."""
    def is_linked(n):
        return linked and linked.search(
            metas[n]["title"] + "\n" + (metas[n].get("body") or ""))
    # Catch-up merges and other huge PRs contain nearly every identifier.
    hay = {n: "\n".join((d["text"], metas[n]["title"], metas[n].get("body") or
                         "", " ".join(d["paths"])))
           for n, d in data.items()
           if len(d["paths"]) <= po.MAX_FILES and not is_linked(n)}
    terms = strong | idents
    df = {t: sum(t in h for h in hay.values()) for t in terms}
    n = len(hay)
    max_df = max(3, math.ceil(n / 100))
    out = []
    for num, h in hay.items():
        hits = [t for t in terms if t in h and df[t] <= max_df]
        if not hits:
            continue
        strong_hits = [t for t in hits if t in strong]
        if not strong_hits and len(hits) < 2:
            continue
        score = sum(math.log(n / df[t]) * (2 if t in strong else 1)
                    for t in hits)
        out.append((round(score, 1), num, sorted(hits, key=lambda t: df[t])))
    return sorted(out, reverse=True), df


def is_self(key, selfs):
    """The issue itself or its copy in the other tracker, matched by key.

    Never by title: recurring test failures share titles, and those are the
    duplicates worth reporting."""
    return key in selfs


def search_issues(terms, selfs):
    """Open GitHub issues mentioning each term, via the search API."""
    found = {}
    for t in terms:
        q = f'repo:{po.REPO} is:issue is:open "{t.replace(chr(34), "")}"'
        args = ("api", "-X", "GET", "search/issues", "-f", f"q={q}",
                "-f", "per_page=20")
        try:
            try:
                r = json.loads(po.gh(*args))
            except po.subprocess.CalledProcessError as e:
                # Search allows 30 requests a minute; bots file issues in
                # bursts. Wait out the window once.
                if "rate limit" not in e.stderr.lower():
                    raise
                time.sleep(SEARCH_RETRY_WAIT)
                r = json.loads(po.gh(*args))
        except po.subprocess.CalledProcessError as e:
            # One failed term (rejected query, still limited) shouldn't stop
            # the others.
            print(f"warning: GitHub search failed: {e.stderr.strip()[:200]}",
                  file=sys.stderr)
            continue
        if r["total_count"] > COMMON_HITS:
            continue
        for it in r["items"]:
            key = f"#{it['number']}"
            if is_self(key, selfs):
                continue
            e = found.setdefault(key, {"title": it["title"],
                                       "url": it["html_url"],
                                       "assignees": [a["login"] for a in
                                                     it["assignees"]],
                                       "terms": []})
            e["terms"].append(t)
    return found


def search_jira(terms, selfs):
    """Unresolved Jira DB issues whose text mentions each term."""
    found = {}
    for t in terms:
        # The term is a phrase inside a JQL string: escape \ and " twice,
        # once for the phrase quotes and once for the JQL string quotes.
        esc = t.replace("\\", "\\\\\\\\").replace('"', '\\\\\\"')
        jql = (f'project = DB AND statusCategory != Done AND '
               f'text ~ "\\"{esc}\\""')
        r = jira_get("search/jql", jql=jql, maxResults=COMMON_HITS + 1,
                     fields="summary,assignee,status")
        issues = r.get("issues", [])
        if len(issues) > COMMON_HITS:
            continue
        for it in issues:
            if is_self(it["key"], selfs):
                continue
            f = it["fields"]
            e = found.setdefault(it["key"], {
                "title": f["summary"], "url": f"{JIRA}/browse/{it['key']}",
                "assignees": [f["assignee"]["displayName"]]
                if f.get("assignee") else [],
                "status": f["status"]["name"], "terms": []})
            e["terms"].append(t)
    return found


def pick_search_terms(strong, idents, df):
    """Strong terms first, then the identifiers rarest among open PRs."""
    ranked = sorted(strong, key=len, reverse=True)
    ranked += sorted(idents, key=lambda t: (df.get(t, 0), -len(t)))
    return ranked[:MAX_SEARCH_TERMS]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--issue", type=int, help="GitHub issue number")
    g.add_argument("--jira", help="Jira key, e.g. DB-26593")
    g.add_argument("--text", help="file with issue text, or - for stdin")
    ap.add_argument("--bases", default=po.DEFAULT_BASES.pattern)
    ap.add_argument("--no-jira", action="store_true",
                    help="skip the Jira search")
    ap.add_argument("--format", choices=("text", "github", "jira"),
                    default="text",
                    help="text (default), or a comment for GitHub or Jira; "
                         "comment formats print nothing when nothing matched")
    ap.add_argument("--post", action="store_true",
                    help="with --jira --format jira: post the comment to the "
                         "Jira issue instead of printing it")
    args = ap.parse_args()
    if args.post and not (args.jira and args.format == "jira"):
        ap.error("--post needs --jira and --format jira")

    if args.format == "github":
        args.no_jira = True     # never put Jira text in a public comment
    have_cred = bool(os.environ.get("JIRA_AUTH")) or os.path.exists(JIRA_TOKEN)
    use_jira = bool(args.jira) or (not args.no_jira and have_cred)
    if use_jira:
        # Jira answers searches anonymously with no results, so a dead token
        # would otherwise look like "no duplicates in Jira".
        try:
            jira_get("myself")
        except urllib.error.HTTPError as e:
            if args.jira:
                sys.exit(f"Jira rejected {jira_cred_source()} (HTTP {e.code}); "
                         "renew it")
            print(f"WARNING: Jira rejected {jira_cred_source()} "
                  f"(HTTP {e.code}); Jira not searched\n")
            use_jira = False

    comment = args.format != "text"
    title, body, selfs = load_issue(args)
    if args.issue and comment and not body.strip():
        # The GitHub copy of an issue filed in Jira: only the sync's lines.
        # Only --jira, run with a Jira credential, can check that issue.
        print("issue body is empty or only Jira sync lines; not checking",
              file=sys.stderr)
        return
    strong, idents = extract_terms(title, body)
    if not strong and not idents:
        msg = "no test names, error text or code identifiers in the issue"
        if comment:
            print(f"{msg}; not commenting", file=sys.stderr)
            return
        sys.exit(msg)

    pool = po.pool_prs(re.compile(args.bases))
    metas = {p["number"]: p for p in pool}
    # Prune only with the full default pool; a narrower --bases would drop
    # the shared cache entries of every PR outside it.
    data = po.load(pool, prune=args.bases == po.DEFAULT_BASES.pattern)
    prs, df = match_prs(strong, idents, data, metas, linked_pattern(selfs))
    terms = pick_search_terms(strong, idents, df)
    # Terms from a Jira issue may be internal, so they never go to GitHub
    # search. Every GitHub issue has a Jira copy, so the Jira search below
    # still finds GitHub-filed duplicates.
    issues = None if args.jira else search_issues(terms, selfs)
    jira = search_jira(terms, selfs) if use_jira else None

    if args.format == "text":
        print_text(strong, terms, prs, metas, issues, jira)
    elif args.post:
        import contextlib, io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            print_comment(args.format, strong, prs, metas, issues, jira,
                          len(strong | idents))
        if buf.getvalue().strip():
            jira_post_comment(args.jira, buf.getvalue())
            print(f"posted to {args.jira}")
        else:
            print("nothing to post")
    else:
        print_comment(args.format, strong, prs, metas, issues, jira,
                      len(strong | idents))


def print_text(strong, terms, prs, metas, issues, jira):
    print(f"Terms: {', '.join(sorted(strong))}" if strong else "Terms: -")
    print(f"Searched issues for: {', '.join(terms)}\n")
    print("## Open PRs that may already fix this")
    for score, n, hits in prs[:10]:
        print(f"- [{score}] {po.describe(metas[n])}\n    matches: "
              f"{', '.join(hits[:8])}")
    if not prs:
        print("- none")
    for head, found in (("Open GitHub issues", issues),
                        ("Unresolved Jira DB issues", jira)):
        print(f"\n## {head} that mention the same things")
        if found is None:
            print("- not searched")
            continue
        rows = sorted(found.items(), key=lambda kv: -len(kv[1]["terms"]))
        for key, e in rows[:10]:
            who = ", ".join(e["assignees"]) or "unassigned"
            st = f", {e['status']}" if "status" in e else ""
            print(f"- {key} {e['title']} ({who}{st}) {e['url']}\n    "
                  f"matches: {', '.join(e['terms'])}")
        if not rows:
            print("- none")


def print_comment(fmt, strong, prs, metas, issues, jira, n_terms):
    """A short comment, or nothing when there is nothing worth saying.

    Keeps only strong matches, at most MAX_ITEMS per section: a test name,
    or two or more shared terms (three for a PR when the issue has
    LONG_ISSUE_TERMS or more). The github format never includes Jira
    results: Jira summaries are internal and GitHub comments are public."""
    # Only a test name is specific enough alone; error text cut at the first
    # number ("plancache reference") matched issues from 2020.
    tests = {t for t in strong if TEST_NAME.fullmatch(t)}

    def keep(terms, need=2):
        return len(terms) >= need or any(t in tests for t in terms)

    pr_need = 3 if n_terms >= LONG_ISSUE_TERMS else 2

    h, b, code = (("### ", "- ", "`") if fmt == "github" else
                  ("h4. ", "* ", "{{"))
    endcode = "`" if fmt == "github" else "}}"
    sections = []
    rows = [(n, hits) for _, n, hits in prs if keep(hits, pr_need)][:MAX_ITEMS]
    if rows:
        sections.append((f"Open PRs that may already fix this", [
            f"{b}#{n} {metas[n]['title']} (@{metas[n]['author']['login']}) "
            f"{metas[n]['url']} - matches "
            + ", ".join(f"{code}{t}{endcode}" for t in hits[:4])
            for n, hits in rows]))
    found = []
    if issues:
        found.append(("Open GitHub issues about the same thing", issues))
    if fmt == "jira" and jira:
        found.append(("Unresolved Jira DB issues about the same thing", jira))
    for head, f in found:
        rows = sorted(((k, e) for k, e in f.items() if keep(e["terms"])),
                      key=lambda kv: -len(kv[1]["terms"]))[:MAX_ITEMS]
        if rows:
            sections.append((head, [
                f"{b}{k} {e['title']} "
                f"({', '.join(e['assignees']) or 'unassigned'}) {e['url']}"
                for k, e in rows]))
    if not sections:
        return
    print("Automated check: this issue looks related to work already in "
          "flight. Please check before starting on it.\n")
    for head, lines in sections:
        print(f"{h}{head}")
        print("\n".join(lines) + "\n")
    print("_Matched on test names, error text and code identifiers in the "
          "issue (.github/scripts/issue_overlap)._" if fmt == "github" else
          "_Matched on test names, error text and code identifiers in the "
          "issue._")

if __name__ == "__main__":
    if any(a == "--jira" or a.startswith("--jira=") for a in sys.argv[1:]):
        # A traceback can show internal Jira text (in a failed command's
        # arguments), and a CI log may be read by people without Jira access.
        # Report the error type only.
        try:
            main()
        except SystemExit:
            raise
        except Exception as e:  # noqa: BLE001
            sys.exit(f"issue_check failed: {type(e).__name__} "
                     "(details suppressed in --jira mode)")
    else:
        main()
