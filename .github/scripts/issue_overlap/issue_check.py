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
import urllib.parse
import urllib.request

import pr_overlap as po

JIRA = "https://yugabyte.atlassian.net"
JIRA_TOKEN = os.path.expanduser("~/.claude/jira-api.token")
# "Github Public Description (Read-Only)": GitHub -> Jira one-way copy of the
# body of an issue filed on GitHub.
GH_DESC_FIELD = "customfield_10885"
MAX_SEARCH_TERMS = 6      # GitHub search allows 30 requests a minute
COMMON_HITS = 25          # a term in more open issues than this says nothing

TEST_NAME = re.compile(
    r"\b(?:[A-Z]\w*Test\w*\.\w+|Test\w+#\w+|org\.yb\.[\w.]+#?\w*)")
ERROR_LINE = re.compile(r"(?:ERROR|FATAL|PANIC|Check failed):\s+(.+)")
URL = re.compile(r"https?://\S+")
HEX_OR_NUM =re.compile(r"\b0x[0-9a-f]+\b|\b\d+\b")
LINKED_ISSUE = re.compile(r"(?:#|issues/)(\d{4,6})\b|\b(DB-\d+)\b")


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
    elif args.jira:
        d = jira_get(f"issue/{args.jira}",
                     fields=f"summary,description,{GH_DESC_FIELD}")
        title = d["fields"]["summary"]
        # An issue filed in Jira has its text in description; one filed on
        # GitHub has an empty description and its text in the synced field.
        body = "\n".join(adf_text(d["fields"].get(k) or {})
                         for k in ("description", GH_DESC_FIELD))
        selfs = {args.jira}
    else:
        body = sys.stdin.read() if args.text == "-" else open(args.text).read()
        title, selfs = "", set()
    # The GitHub issue and its Jira mirror are the same issue.
    for m in LINKED_ISSUE.finditer(body[:300]):
        if m.group(2) and "Jira Link" in body[:300]:
            selfs.add(m.group(2))
    return title, body, selfs


def extract_terms(title, body):
    """Strong terms (test names, error text) and code identifiers."""
    # Tokens inside links (Jira atlOrigin, %2F-encoded paths) are not terms.
    text = URL.sub(" ", f"{title}\n{body}")
    strong = set(TEST_NAME.findall(text))
    for m in ERROR_LINE.finditer(text):
        # Keep the error text up to the first address or number, which
        # changes from run to run.
        phrase = HEX_OR_NUM.split(m.group(1))[0].strip(" .:'\"`")
        if len(phrase) >= 15:
            strong.add(phrase)
    idents = {t for t in po.IDENT.findall(text)
              if t not in po.STOP and po.CODE_LIKE.search(t)}
    idents -= {t for s in strong for t in po.IDENT.findall(s)}
    return strong, idents


def match_prs(strong, idents, data, metas):
    """Score pool PRs by the issue terms their diff, title or body contain."""
    hay = {n: "\n".join((d["text"], metas[n]["title"], metas[n].get("body") or
                         "", " ".join(d["paths"])))
           for n, d in data.items()}
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


def is_self(key, title, selfs, self_title):
    """The issue itself, or its GitHub/Jira mirror (same title)."""
    return key in selfs or (self_title and title.strip() == self_title.strip())


def search_issues(terms, selfs, self_title):
    """Open GitHub issues mentioning each term, via the search API."""
    found = {}
    for t in terms:
        q = f'repo:{po.REPO} is:issue is:open "{t}"'
        r = json.loads(po.gh("api", "-X", "GET", "search/issues", "-f",
                             f"q={q}", "-f", "per_page=20"))
        if r["total_count"] > COMMON_HITS:
            continue
        for it in r["items"]:
            key = f"#{it['number']}"
            if is_self(key, it["title"], selfs, self_title):
                continue
            e = found.setdefault(key, {"title": it["title"],
                                       "url": it["html_url"],
                                       "assignees": [a["login"] for a in
                                                     it["assignees"]],
                                       "terms": []})
            e["terms"].append(t)
    return found


def search_jira(terms, selfs, self_title):
    """Unresolved Jira DB issues whose text mentions each term."""
    found = {}
    for t in terms:
        jql = (f'project = DB AND statusCategory != Done AND '
               f'text ~ "\\"{t}\\""')
        r = jira_get("search/jql", jql=jql, maxResults=20,
                     fields="summary,assignee,status")
        issues = r.get("issues", [])
        if len(issues) >= 20:
            continue
        for it in issues:
            if is_self(it["key"], it["fields"]["summary"], selfs, self_title):
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
                sys.exit(f"Jira rejected {JIRA_TOKEN} (HTTP {e.code}); renew it")
            print(f"WARNING: Jira rejected {JIRA_TOKEN} (HTTP {e.code}); "
                  "Jira not searched\n")
            use_jira = False

    title, body, selfs = load_issue(args)
    strong, idents = extract_terms(title, body)
    if not strong and not idents:
        sys.exit("no test names, error text or code identifiers in the issue")

    pool = po.pool_prs(re.compile(args.bases))
    metas = {p["number"]: p for p in pool}
    data = po.load(pool)
    prs, df = match_prs(strong, idents, data, metas)
    terms = pick_search_terms(strong, idents, df)
    issues = search_issues(terms, selfs, title)
    jira = search_jira(terms, selfs, title) if use_jira else None

    if args.format == "text":
        print_text(strong, terms, prs, metas, issues, jira)
    elif args.post:
        import contextlib, io
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            print_comment(args.format, strong, prs, metas, issues, jira)
        if buf.getvalue().strip():
            jira_post_comment(args.jira, buf.getvalue())
            print(f"posted to {args.jira}")
        else:
            print("nothing to post")
    else:
        print_comment(args.format, strong, prs, metas, issues, jira)


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
            print("- not searched (no working Jira token)")
            continue
        rows = sorted(found.items(), key=lambda kv: -len(kv[1]["terms"]))
        for key, e in rows[:10]:
            who = ", ".join(e["assignees"]) or "unassigned"
            st = f", {e['status']}" if "status" in e else ""
            print(f"- {key} {e['title']} ({who}{st}) {e['url']}\n    "
                  f"matches: {', '.join(e['terms'])}")
        if not rows:
            print("- none")


def print_comment(fmt, strong, prs, metas, issues, jira):
    """A short comment, or nothing when there is nothing worth saying.

    Keeps only strong matches (two or more terms, or a test name), at most
    five per section. The github format never includes Jira results: Jira
    summaries are internal and GitHub comments are public."""
    # Only a test name is specific enough alone; error text cut at the first
    # number ("plancache reference") matched issues from 2020.
    tests = {t for t in strong if TEST_NAME.fullmatch(t)}

    def keep(terms):
        return len(terms) >= 2 or any(t in tests for t in terms)

    h, b, code = (("### ", "- ", "`") if fmt == "github" else
                  ("h4. ", "* ", "{{"))
    endcode = "`" if fmt == "github" else "}}"
    sections = []
    rows = [(n, hits) for _, n, hits in prs if keep(hits)][:5]
    if rows:
        sections.append((f"Open PRs that may already fix this", [
            f"{b}#{n} {metas[n]['title']} (@{metas[n]['author']['login']}) "
            f"{metas[n]['url']} - matches "
            + ", ".join(f"{code}{t}{endcode}" for t in hits[:4])
            for n, hits in rows]))
    found = [("Open GitHub issues about the same thing", issues)]
    if fmt == "jira" and jira:
        found.append(("Unresolved Jira DB issues about the same thing", jira))
    for head, f in found:
        rows = sorted(((k, e) for k, e in f.items() if keep(e["terms"])),
                      key=lambda kv: -len(kv[1]["terms"]))[:5]
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
    main()
