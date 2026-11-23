# issue_overlap

`issue_check.py` lists open PRs and issues that look like the same work as a newly filed issue.
The `issue-overlap` workflow runs it on every new issue and comments with what it finds.
`pr_overlap.py` does the same for a PR (open PRs changing the same code), and `issue_check.py`
reuses its PR-diff index.

```bash
python3 issue_check.py --issue 38288                    # plain-text report
python3 issue_check.py --issue 38288 --format github    # the comment the workflow would post
python3 issue_check.py --jira DB-26593 --format jira    # needs JIRA_AUTH="email:token"
python3 pr_overlap.py 38804                             # open PRs changing the same code
```

## How a match is decided

- Terms come from the issue: test names, error text after `ERROR:`/`FATAL:`/`Check failed:`
  (cut at the first number, address or quoted name), and snake_case/CamelCase identifiers.
  Tokens in URLs and the lines the Jira sync adds are ignored.
- PRs are matched against their diff, title, body and paths. PRs that already reference the
  issue (`#N` or its Jira key) are skipped, since GitHub shows them on the issue. Issues are
  matched through GitHub search and Jira JQL on the six most specific terms. A term that more
  than 1% of open PRs, or more than 25 issues in either tracker, contain is ignored.
- A comment lists an item only if it shares a test name or at least two terms with the issue.
  A PR needs three when the issue has 20 or more terms, because long reports share vocabulary
  with many PRs. A comment shows at most three items per section. The script prints nothing and
  exits 0 when nothing qualifies, when the issue has no terms, and for the GitHub copy of a
  Jira-filed issue (the `jira-issue` job checks that one).
- **Rollout:** the workflow posts only when the repository variable `ISSUE_OVERLAP_POST` is
  `true`. Until then, GitHub-issue runs write the comment they would post to the job summary,
  and Jira-issue runs log only whether they would post.
- **Jira text stays in Jira.** GitHub comments never include Jira results. In `--jira` mode,
  terms are never sent to GitHub search (each GitHub issue has a Jira copy, so the Jira search
  finds GitHub-filed duplicates), and errors print only their type, because the Actions log
  is public.

## Gotchas

- An issue filed in Jira has its text in `description`. An issue filed on GitHub has an empty
  `description` and its text in "Github Public Description (Read-Only)" (`customfield_10885`).
  `--jira` reads both.
- Jira answers a search from a rejected token with an empty result, not an error, so the script
  checks the token first and fails loudly.
- The first run on an empty cache fetches the changed files of every open PR (about 600 API
  calls). Later runs fetch only PRs whose head SHA changed. The workflow saves one cache entry
  a day.
- On a sample of 12 new issues (2026-11-23) the check would comment on 4. A PR whose diff lists
  many command names (#36927, a yb-admin help registry) still matches unrelated xCluster
  issues; check the job summaries for such cases before turning posting on.
- GitHub search allows 30 requests a minute and a run makes up to six. When bots file issues
  in a burst, a search waits a minute and retries once.
