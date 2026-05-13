## Making changes and pushing to upstream
Never operate on master branch directly. Always create a new local branch and work on that.
Pushing the branch to yugabyte/yugabyte-db repository is not allowed. If you are on a personal fork, you can push to that.
arc and phorge are used to review, run lab tests, and merge changes. This should ONLY be done by the human.

Avoid using non-ASCII characters in files and commit messages.
There may be some exceptions where appropriate such as `collate.icu.utf8.sql` and `jsonpath_encoding.out`.

## Confidentiality — never leak customer data, YugabyteDB secrets, or PII

The `yugabyte/yugabyte-db` repo is public. Every GitHub issue, PR, comment, commit message, code comment, **test file, test fixture, golden output**, and file name landed on it is read by anyone on the internet, and indexed by search engines. **Phorge is also treated as public** — diff titles, summaries, test plans, inline comments, and the code/tests attached to a diff must follow the same rules as the public repo (the title, summary, test plan, and test code land on the public repo verbatim when upstreamed anyway). **Treat anything you (the agent) write that lands in a public destination — the public repo, public GitHub issues/PRs/comments, Phorge diffs, public mailing lists, public Slack channels, public gists — as permanent and unretractable.** Confidential material must never appear in those destinations. This rule is non-negotiable and overrides convenience — if following it means writing a slower synthetic reproducer instead of pasting the customer's repro, write the synthetic reproducer.

**Internal destinations are different.** Direct chat with the user, internal JIRA tickets, internal Slack threads, and internal support cases are appropriate places to discuss customer specifics with the user — quote the customer name, paste the real schema, attach the real log if needed. The rule below is about what crosses into a public artifact, not about what you tell the human you're working with.

**Never write any of the following into a GitHub issue title/body/comment, PR title/description/comment, Phorge diff title/summary/test plan/comment, commit subject or body, code or test comment, test code, test fixture, test data file, golden output file, log example, fixture YAML, mock response, or file name:**

- **Customer-identifying information** — company names, account IDs, universe/cluster UUIDs or names, environment names, region/zone names tied to a customer deployment, or any string that ties a behavior back to a specific customer. Substitute with `customer-1`, `acme-corp`, a generic description, or omit. This includes filenames: do **not** name a test `test_repro_acme_orders.cc`.
- **PII** — real emails, names, phone numbers, postal addresses, real IP addresses (public **or** private — a customer's `10.x` address is still theirs). In tests, use only the documentation ranges: IPv4 `192.0.2.0/24` / `198.51.100.0/24` / `203.0.113.0/24` (RFC 5737), IPv6 `2001:db8::/32` (RFC 3849), hostnames `example.com` / `example.org` / `example.net` (RFC 2606), names `Alice`/`Bob`/`Carol`, phone numbers `555-0100`–`555-0199`.
- **Unanonymized customer schemas, queries, plans, or data** — table names, column names, query text, query plans (including `EXPLAIN` output), sample rows, JSON payloads, or any byte taken from a real customer. Reconstruct a minimal reproducer with synthetic identifiers (`t1`, `users`, `id`, `value`) that demonstrates the same defect. Renaming a single column is **not** anonymization if the rest of the schema is intact and recognizable.
- **Secrets and credentials** — API keys, tokens, passwords, TLS certificates, private keys, license keys, kubeconfigs, production S3 bucket names, internal-only Slack/JIRA/Linear URLs, internal hostnames, vault paths. This includes test fixtures: don't bake a real-looking access key into a mock — use obvious placeholders like `AKIAIOSFODNN7EXAMPLE` (AWS docs example) or `dummy_token`.
- **YugabyteDB-internal information not yet public** — unreleased roadmap details, internal SLAs, embargoed security findings, internal infra hostnames.

### Tests are not exempt

It is a common mistake to think "the test file isn't the PR description, so the customer's data is fine here." It is not fine. The test file is checked into the public repo, indexed, and stays there forever. **Every example value in a test must either be obviously synthetic or pulled from a documented public range (RFC 5737/3849/2606, AWS `EXAMPLE` keys, etc.).** Specifically:

- **Schema in tests** — rename every table and column. If the customer hit the bug on `orders_2024_q3` with column `pii_email_addr`, the test should use `t` with column `email` (or `c1` if the name is incidental to the bug).
- **Data in tests** — replace real emails with `user@example.com`, real names with `Alice`/`Bob`, real IPs with `192.0.2.1`, real phone numbers with `555-0100`–`555-0199` (the documentation range), real UUIDs with `10000000-2000-3000-4000-000000000005`.
- **File and test names** — don't embed a customer identifier in the filename, test name, or test fixture path. `test_acme_replication_lag.cc` leaks the customer's name in `git log` even if every byte of the body is sanitized.
- **Code comments** — `// repro for ACME's outage on 2026-04-12` leaks the customer. Write `// repro for replication-lag regression (see PLAT-20518)` instead.
- **Golden output and log fixtures** — output captured from a customer system almost certainly contains a hostname, IP, UUID, or schema name that identifies them. Regenerate the golden file by running the synthetic reproducer locally; don't paste the customer's log.
- **Stack traces and crash dumps** — paths like `/home/yugabyte/acme-prod-1/...` or hostnames in frames leak the source. Re-run locally and capture a clean trace.

### When the source material is a customer report

1. Read the original report from the internal source (JIRA, support ticket, Slack thread) — never paste or link it from a public artifact (issue, PR, diff, commit, code comment).
2. Reproduce the defect locally with synthetic inputs (synthetic schema, synthetic data, synthetic cluster names).
3. Land only the synthetic reproducer in the test, the PR description, and the commit message.
4. Reference the issue by internal ticket ID only (e.g., `PLAT-20518`); don't quote customer-facing text from the ticket.
5. If the bug is impossible to reproduce without customer-specific state, **stop and ask the user** — don't paste the state into a public artifact as a workaround.

### Scrub checklist before any public publish

Before running `create-pr.sh`, `arc diff --create`, `gh pr comment`, `gh issue create`, or any push of a branch that has an open PR, scan the new content (diffs, message files, comments) for:

- [ ] No customer company names, account IDs, universe/cluster names or UUIDs, support case numbers, environment names, region/zone names.
- [ ] No real email addresses, names, phone numbers, postal addresses, or IPs (public or private).
- [ ] No customer-derived table names, column names, SQL text, query plans, JSON payloads, or sample rows.
- [ ] No tokens, API keys, passwords, certificates, private keys, license keys, kubeconfigs, or production S3 bucket names.
- [ ] No internal-only hostnames, URLs, Grafana/Slack/Linear links, or vault paths.
- [ ] No unreleased internal information (roadmap, SLAs, embargoed security findings, internal infra hostnames).
- [ ] Test filenames, test function names, and golden-output files reviewed too — not just the prose.

**If you're unsure whether a string is sensitive, don't write it down — ask the user.** A leaked customer name in a public PR can't be unsent; a clarifying question costs nothing.

## Git workflow on branches with an open PR

This repo **squash-merges** every PR, so the per-commit history of a branch is purely a review aid — reviewers read each commit independently to follow the change. Optimize for that:

- **Always add a new commit** for follow-up work (lint fixes, review feedback, additional changes). Do **not** `git commit --amend` an existing commit on a branch that has been pushed.
- **Batch related changes into one commit; don't split per-file, per-comment, or per-round.** A new commit should mark a distinct logical unit of work, not a step in a single response to review. If a review pass surfaces five suggestions and you're applying four of them, that's one commit (`Address review feedback: <one-line summary>`) — not four. If a lint warning slipped into the prior commit, fold the fix into the *next* logical commit you'd push anyway rather than emitting a standalone `Fix lint` commit. The squash-merge collapses everything at merge time, but the PR-level history is what reviewers read; many tiny commits make that harder, not easier.
- **Don't collapse commits locally yourself** (no `git rebase -i ... squash`, no hand-merging two commits into one). Each follow-up should stay as its own commit so reviewers can read it independently; the squash happens automatically at merge time on GitHub.
- The first commit on a branch can carry the PR's overall message style; subsequent commits should have short, scoped subject lines (`Fix lint warnings`, `Address review: rename foo to bar`) so reviewers can tell what each commit is for.
- **`## Upgrade/Rollback safety` section in PR descriptions.** Required for any change that affects upgrade-rollback compatibility — wire-format (`.proto`) changes, gflag default flips that alter observable behavior, catalog schema bumps, on-disk-format changes, RPC-versioning tweaks, migration scripts. The section must spell out forward/backward behavior on a mixed-version cluster and what rollback looks like. `create-pr.sh` enforces this mechanically for `.proto` file changes (`exit 1` if missing). For non-proto upgrade-relevant changes the rule is soft — include the section anyway; reviewers should ask for it.
- **PR titles must match `[<issue>] <Component>: <Title>`.** `<issue>` is `#NNNN` for GitHub issues or `PROJECT-NNN` for JIRA. `<Component>` is one of `DocDB`, `YSQL`, `YCQL`, `YBA`, `CDC`, `xCluster`, `yugabyted`, `Docs`, `ClaudeCode`, `Build` (or another component agreed with the team). `create-pr.sh` enforces this format mechanically — invalid titles abort with `exit 1` before any push or PR creation. Backport titles produced by `backport-commit.sh` keep the same shape with a `[BACKPORT <release-branch>]` prefix.
- **Always push via `.agents/scripts/git-push.sh`** — never via raw `git push`. The helper integrates any commits already on the fork branch, rebases onto the latest upstream `<base>`, runs lint, and force-pushes (`--force-with-lease`) since the rebase rewrote SHAs. Direct `git push` is **deny-listed** in `.claude/settings.json` so the only path to publishing a commit is through this helper. (The deny only applies to `Bash` invocations the agent makes; `git-push.sh`'s internal `git push` runs as a subprocess and is unaffected.) Force-pushing is the expected behavior of every push; don't try to avoid it. Reviewers' line comments may flip to "outdated" after a rebase that touches their lines — that's the accepted tradeoff for keeping the branch current with master.
- **Keep the PR summary in sync — automatically.** After pushing follow-up commits to an existing PR, evaluate the new commits against the existing body. If they **change scope, change approach, or invalidate the test plan**, update the body directly via the `gh api -X PATCH` template that `git-push.sh` prints. **Do not ask the user for permission first** — the rules in this list determine when an update is needed; if they apply, just apply them. Leave the summary alone for lint fixes, typo fixes, comment-only edits, and pure-refactor commits that don't shift what the PR claims to do (these are common enough that asking each time is annoying). **Edit the cached body file with the Edit tool, not `python3`/`sed`/`awk` shell-outs.** Workflow: `gh pr view <num> -R <repo> --json body -q .body > /tmp/claude/pr-body-<num>.md` (allowed via `gh pr view`), then use the Edit tool on that path (allowed via `Edit(/tmp/claude/**)`), then `jq -Rs '{body: .}' < ... | gh api -X PATCH /repos/<repo>/pulls/<num> --input -` (both allowed). `python3 -c '...'` is *not* allowlisted (intentionally — too broad) and will prompt every time.
- **Keep the PR title in sync — sparingly.** `git-push.sh` also surfaces the current title alongside the new commits. Update the title **only when the new commits would make the existing title misleading** — e.g., the component changed (`DocDB` → `YBA`), the work expanded substantially beyond what the title claims, or the issue reference is wrong. **Do not retitle for refinements within the existing scope, added tests, or rewordings for taste** — title churn invalidates GitHub notifications, breaks bookmarks, and creates review-tool noise. Title format must still match `[<issue>] <Component>: <Title>`. Update via `gh api -X PATCH /repos/<repo>/pulls/<num> -f title='<new title>'` (allowed via the same scoped pulls PATCH rule the body update uses). Don't ask the user first; the "misleading vs. refinement" call is yours.

## Agent signature on automated output

Whenever you (the agent) **autonomously author** any of the following, append the signature block below at the end:

- A reply to a PR review comment (`gh api -X POST .../comments/<id>/replies`).
- A standalone PR conversation comment (`gh pr comment ...`).
- A commit message you authored (commit body, **not** subject — subject stays clean).

```
---
_automated · <agent>_
```

Substitute `<agent>` with your own identifier — e.g. `Claude Code (Opus 4.7)`, `Cursor`, `Codex`, `Aider`. Use the most specific identifier you can (model name + harness) so a reader can tell which agent wrote the line. If you don't know your model, the harness name alone (`Claude Code`, `Cursor`, etc.) is fine.

**Skip the signature on:**
- The commit subject line (one-liner stays terse).
- Direct chat with the user (not an automated post).

The point is to make agent-authored output legible to humans skimming the PR or `git log` — so reviewers know whether a comment is a teammate's nuance or a bot's heuristic, and so blame on a commit reflects that an agent drafted it.

## Reviewing commits

When reviewing a PR, a backport diff, or any commit on this repo, follow the guidance in `.gemini/styleguide.md`. It scopes which issues are worth flagging (correctness, memory safety, security, concurrency, performance, breaking-change risk) versus which to skip (lint-owned style, naming nits, repeat-flags across rounds), spells out the **backport-mode** rules for cherry-pick PRs (compare against the originating commit; don't re-flag byte-identical hunks), and lists the areas (`src/yb/master/`, `src/yb/tablet/`, `src/yb/cdc/`, `src/postgres/`, `managed/`) that warrant deeper attention. Read it before forming review comments.

## Replying to PR review comments

After addressing review comments in follow-up commits, follow these rules:

- **Don't post trivial replies** like `Done`, `Fixed`, `Addressed in <sha>`. Resolving the thread carries that signal; an empty acknowledgement reply is noise.
- **Do post a reply when the suggestion was wrong or doesn't apply** — explain briefly why you're declining. Silently leaving a wrong suggestion unresolved looks like an oversight.
- **Do post a reply when the fix you shipped differs from what the suggestion proposed** — reviewers benefit from knowing why the implemented fix isn't the suggested one.
- **Mark threads resolved after addressing** via the GitHub UI or:
  ```
  gh api graphql -f query='mutation { resolveReviewThread(input: {threadId: "<id>"}) { thread { id } } }'
  ```
  Look up `threadId`s with the `pullRequest.reviewThreads` GraphQL query against the PR.

## Scratch files

Write temporary files (PR bodies, commit messages, lint logs, etc.) to **`/tmp/claude/`** rather than `/tmp/` directly. The folder is allowlisted in `.claude/settings.json` for `Read`, `Write`, `Edit`, and `Bash(cat > /tmp/claude/*)` / `Bash(mkdir -p /tmp/claude*)`, so writes there don't trigger a permission prompt. Pick a filename that disambiguates across concurrent uses (include the issue number, PR number, or task name — e.g. `/tmp/claude/pr-body-31407.md`, not `/tmp/claude/body.md`). Run `mkdir -p /tmp/claude` once if it doesn't exist.

## Build Prerequisites for Claude Code

Before building YugabyteDB in a Claude Code session, install the following dependencies:

- **CMake >= 3.31** — Ubuntu 24.04's default apt package is too old (3.28). Install via pip:
  ```bash
  pip3 install 'cmake>=3.31'
  ```
- **rsync**
- **gettext** (provides `msgfmt`, required by postgres NLS configure)
- **en_US.UTF-8 locale** — required by `initdb`; minimal containers often lack it

On Ubuntu/Debian (`apt-get` has DNS issues in Claude Code web sessions — use `curl` + `dpkg` instead):
```bash
sudo locale-gen en_US.UTF-8
pushd /tmp
curl -L -o gettext-base.deb "http://archive.ubuntu.com/ubuntu/pool/main/g/gettext/gettext-base_0.21-14ubuntu2_amd64.deb"
curl -L -o gettext.deb "http://archive.ubuntu.com/ubuntu/pool/main/g/gettext/gettext_0.21-14ubuntu2_amd64.deb"
curl -L -o libpopt0.deb "http://archive.ubuntu.com/ubuntu/pool/main/p/popt/libpopt0_1.19+dfsg-1build1_amd64.deb"
curl -L -o rsync.deb "http://security.ubuntu.com/ubuntu/pool/main/r/rsync/rsync_3.2.7-1ubuntu1.2_amd64.deb"
sudo dpkg -i gettext-base.deb gettext.deb libpopt0.deb rsync.deb
popd
```

## Build System

The primary build entry point is `yb_build.sh` at the repository root.

Reuse existing build compiler/type if available (see `build/latest` symlink); default to `release` otherwise.

Add these `yb_build.sh` options to reduce build time:
- Specify only the cmake targets you need (for example, `daemons initdb`).
- Skip java build (`--sj`) unless you have to run java tests.
- Skip pg_parquet build (`--skip-pg-parquet`) unless you need it.
- Skip odyssey build (`--no-odyssey`) unless you need it.
- Skip YBC build (`--no-ybc`) unless you need it.

The first build takes approximately 20 minutes. Incremental builds are much faster.

Always pipe build output to a temp file so you can inspect errors without rebuilding:
```bash
./yb_build.sh release daemons initdb --sj --skip-pg-parquet --no-odyssey --no-ybc 2>&1 | tee /tmp/yb-build.log
```

Pitfalls when doing incremental build:
- The `initdb` cmake target may not be built when specified in the same `yb_build.sh` command as test options.
  In this case, build `initdb` first in a separate command before running tests.
- Forgetting the `reinitdb` cmake target after changes to the system catalog since last build may cause failures.
- Forgetting `--clean` after changes to third-party since last build may cause failures.

Further information is in [the docs page build-and-test](../docs/content/stable/contribute/core-database/build-and-test.md) (may be stale).

### Common Build Commands

```bash
./yb_build.sh release reinitdb
```

```bash
./yb_build.sh release daemons initdb --sj --skip-pg-parquet --no-odyssey --no-ybc
```

### C++ Tests

```bash
./yb_build.sh release --cxx-test tablet-test

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter TestLoadBalancerPreferredLeader.TestBalancingMultiPriorityWildcardLeaderPreference

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter "*Wildcard*"

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test -n 10
```

### Java Tests

```bash
./yb_build.sh release --java-test org.yb.client.TestYBClient

./yb_build.sh release --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```
