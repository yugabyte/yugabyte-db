# issue_overlap

`issue_check.py` lists open PRs and issues that look like the same work as a newly filed issue.
The `issue-overlap` workflow runs it on every new GitHub issue and comments with what it finds.
`pr_overlap.py` does the same for a PR (open PRs changing the same code), and `issue_check.py`
reuses its PR-diff index.

```bash
python3 issue_check.py --issue 38288                    # plain-text report
python3 issue_check.py --issue 38288 --format github    # the comment the workflow would post
python3 pr_overlap.py 38804                             # open PRs changing the same code
```

## How a match is decided

- Terms come from the issue: test names, error text after `ERROR:`/`FATAL:`/`Check failed:`
  (cut at the first number, address or quoted name), and snake_case/CamelCase identifiers.
  Tokens in URLs are ignored.
- PRs are matched against their diff, title, body and paths. A PR whose title or body already
  names the issue (`#N`) is skipped: its author has linked it already. Issues are matched
  through GitHub search on the six most specific terms. A term that more than 1% of open PRs,
  or more than 25 open issues, contain is ignored.
- A comment lists an item only if it shares a test name or at least two terms with the issue.
  A PR needs three when the issue has 20 or more terms, because long reports share vocabulary
  with many PRs. A comment shows at most three items per section. The script prints nothing and
  exits 0 when nothing qualifies or the issue has no terms. An issue filed in Jira reaches
  GitHub with only its title, so it is checked on the title alone.
- **Rollout:** the workflow posts only when the repository variable `ISSUE_OVERLAP_POST` is
  `true`. Until then, each run writes the comment it would post to the job summary.

## Gotchas

- The first run on an empty cache fetches the changed files of every open PR (about 600 API
  calls). Later runs fetch only PRs whose head SHA changed. The workflow saves one cache entry
  a day.
- On a sample of 12 new issues (2026-11-23) the check would comment on 4. A PR whose diff lists
  many command names (#36927, a yb-admin help registry) still matches unrelated xCluster
  issues; check the job summaries for such cases before turning posting on.
- GitHub search allows 30 requests a minute and a run makes up to six. When bots file issues
  in a burst, a search waits a minute and retries once.
