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
  (cut at the first number or address), and snake_case/CamelCase identifiers. Tokens in URLs are
  ignored.
- PRs are matched against their diff, title, body and paths. Issues are matched through GitHub
  search and Jira JQL on the six most specific terms. A term that more than 1% of open PRs (or
  more than 25 issues) contain is ignored.
- A comment lists an item only if it shares a test name or at least two terms with the issue.
  It shows at most five items per section and posts nothing when nothing qualifies.
- **GitHub comments never include Jira results.** Jira summaries are internal. Jira comments
  include both.

## Gotchas

- An issue filed in Jira has its text in `description`. An issue filed on GitHub has an empty
  `description` and its text in "Github Public Description (Read-Only)" (`customfield_10885`).
  `--jira` reads both.
- Jira answers a search from a rejected token with an empty result, not an error, so the script
  checks the token first and fails loudly.
- The first run on an empty cache fetches the changed files of every open PR (about 600 API
  calls). Later runs fetch only PRs whose head SHA changed.
