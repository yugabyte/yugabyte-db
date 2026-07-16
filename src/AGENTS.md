## Making changes and pushing to upstream
Never operate on master branch directly. Always create a new local branch and work on that.
Pushing the branch to yugabyte/yugabyte-db repository is not allowed. If you are on a personal fork, you can push to that.
arc and phorge are used to review, run lab tests, and merge changes. This should ONLY be done by the human.

Avoid using non-ASCII characters in files and commit messages.
There may be some exceptions where appropriate such as `collate.icu.utf8.sql` and `jsonpath_encoding.out`.

## Confidentiality — never leak customer data, YugabyteDB secrets, or PII

The `yugabyte/yugabyte-db` repo is public, and **Phorge is treated as public too** (diff titles, summaries, test plans, and attached code land on the public repo verbatim when upstreamed). Anything you write to a public destination — repo, issues, PRs, comments, commit messages, Phorge diffs, mailing lists, public Slack, gists — is permanent and unretractable. This rule overrides convenience: if it means writing a slower synthetic reproducer instead of pasting the customer's repro, write the synthetic reproducer.

Internal destinations are different: direct chat with the user, internal JIRA, internal Slack, and support cases are fine places to discuss customer specifics. The rules below are about what crosses into a public artifact.

**Never write any of the following into any public artifact — including test code, test fixtures, test data, golden outputs, code comments, branch names, and file names, not just prose:**

- **Customer-identifying information** — company names, account IDs, universe/cluster UUIDs or names, environment names, region/zone names tied to a customer deployment, or any string that ties a behavior back to a specific customer. Substitute with `customer-1`, `acme-corp`, a generic description, or omit.
- **PII** — real emails, names, phone numbers, postal addresses, real IP addresses (public **or** private — a customer's `10.x` address is still theirs). Use only documented public ranges: IPv4 `192.0.2.0/24` / `198.51.100.0/24` / `203.0.113.0/24` (RFC 5737), IPv6 `2001:db8::/32` (RFC 3849), hostnames `example.com`/`.org`/`.net` (RFC 2606), names `Alice`/`Bob`/`Carol`, phones `555-0100`–`555-0199`.
- **Unanonymized customer schemas, queries, plans, or data** — table names, column names, query text, `EXPLAIN` output, sample rows, JSON payloads, or any byte taken from a real customer. Reconstruct a minimal reproducer with synthetic identifiers (`t1`, `users`, `id`, `value`). Renaming a single column is **not** anonymization if the rest of the schema is recognizable.
- **Secrets and credentials** — API keys, tokens, passwords, TLS certificates, private keys, license keys, kubeconfigs, production S3 bucket names, internal-only Slack/JIRA/Linear URLs, internal hostnames, vault paths. In fixtures use obvious placeholders like `AKIAIOSFODNN7EXAMPLE` (AWS docs example) or `dummy_token`.
- **YugabyteDB-internal information not yet public** — unreleased roadmap details, internal SLAs, embargoed security findings, internal infra hostnames.

Common leak paths that slip past a scrub of the prose:

- **Tests are not exempt.** A test file is checked into the public repo, indexed, and stays there forever. Every example value must be obviously synthetic or from a documented public range. If the customer hit the bug on `orders_2024_q3` with column `pii_email_addr`, the test uses `t` with column `email`. `// repro for ACME's outage on 2026-04-12` leaks the customer — write `// repro for replication-lag regression (see PLAT-20518)`.
- **File and test names.** `test_acme_replication_lag.cc` leaks the customer in `git log` even if every byte of the body is sanitized.
- **Golden outputs, log fixtures, stack traces.** Output captured from a customer system almost certainly contains an identifying hostname, IP, UUID, path, or schema name. Regenerate by running the synthetic reproducer locally; don't paste the customer's capture.
- **Branch names.** A branch name isn't stored in any commit, so it slips past every diff scrub — then becomes the PR's public head ref (PR page, URL, refs) the moment it's pushed. Name the branch after the change, not the customer: `fix-replication-lag` or `gh-31609`, never `fix-acme-replication-lag`. Rename with `git branch -m <new-name>` before pushing.

When the source material is a customer report:

1. Read the original report from the internal source (JIRA, support ticket, Slack thread) — never paste or link it from a public artifact.
2. Reproduce the defect locally with synthetic inputs (synthetic schema, data, cluster names).
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
- [ ] Branch name describes the change, not a customer — it becomes the public PR head ref when the branch is pushed.

**If you're unsure whether a string is sensitive, don't write it down — ask the user.** A leaked customer name in a public PR can't be unsent; a clarifying question costs nothing.

## Git workflow on branches with an open PR

This repo **squash-merges** every PR, so the per-commit history of a branch is purely a review aid — reviewers read each commit independently to follow the change. Optimize for that:

- **Always add a new commit** for follow-up work (lint fixes, review feedback, additional changes). Do **not** `git commit --amend` an existing commit on a branch that has been pushed.
- **Batch related changes into one commit; don't split per-file, per-comment, or per-round.** Applying four review suggestions is one commit (`Address review feedback: <one-line summary>`), not four. Fold a stray lint fix into the next logical commit rather than emitting a standalone `Fix lint` commit.
- **Don't collapse commits locally yourself** (no `git rebase -i ... squash`, no hand-merging commits). The squash happens automatically at merge time on GitHub.
- The first commit on a branch can carry the PR's overall message style; subsequent commits should have short, scoped subject lines (`Fix lint warnings`, `Address review: rename foo to bar`).
- **`## Upgrade/Rollback safety` section in PR descriptions.** Required for any change that affects upgrade-rollback compatibility — wire-format (`.proto`) changes, gflag default flips that alter observable behavior, catalog schema bumps, on-disk-format changes, RPC-versioning tweaks, migration scripts. Spell out forward/backward behavior on a mixed-version cluster and what rollback looks like. `create-pr.sh` enforces this mechanically for `.proto` changes; for other upgrade-relevant changes the rule is soft — include the section anyway.
- **PR titles must match `[<issue>] <Component>: <Title>`.** `<issue>` is `#NNNN` for GitHub issues or `PROJECT-NNN` for JIRA. `<Component>` is one of `DocDB`, `YSQL`, `YCQL`, `YBA`, `CDC`, `xCluster`, `yugabyted`, `Docs`, `ClaudeCode`, `Build` (or another component agreed with the team). `create-pr.sh` enforces this mechanically. Backport titles keep the same shape with a `[BACKPORT <release-branch>]` prefix.
- **Always push via `.agents/scripts/git-push.sh`** — never via raw `git push` (deny-listed in `.claude/settings.json`). The helper integrates commits already on the fork branch, rebases onto the latest upstream `<base>`, runs lint, and force-pushes (`--force-with-lease`). Force-pushing is the expected behavior of every push; don't try to avoid it.
- **Keep the PR summary in sync — automatically.** After pushing follow-up commits, update the PR body if they **change scope, change approach, or invalidate the test plan** — don't ask the user first; these rules determine when an update is needed. Leave it alone for lint fixes, typos, comment-only edits, and pure refactors. Command mechanics: [.agents/docs/pr-metadata-sync.md](../.agents/docs/pr-metadata-sync.md).
- **Keep the PR title in sync — sparingly.** Update the title **only when the new commits make it misleading** (component changed, scope expanded substantially, wrong issue reference) — not for refinements, added tests, or rewordings; title churn invalidates notifications and creates review noise. The "misleading vs. refinement" call is yours. Same mechanics doc as above.

## Agent signature on automated output

Whenever you (the agent) **autonomously author** any of the following, append the signature block below at the end, so humans skimming the PR or `git log` can tell agent-authored output from a teammate's:

- A reply to a PR review comment (`gh api -X POST .../comments/<id>/replies`).
- A standalone PR conversation comment (`gh pr comment ...`).
- A commit message you authored (commit body, **not** subject — subject stays clean).

```
---
_automated · <agent>_
```

Substitute `<agent>` with the most specific identifier you have (model name + harness, e.g. `Claude Code (Opus 4.7)`); the harness name alone is fine if you don't know your model. Skip the signature on the commit subject line and in direct chat with the user.

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

Write temporary files (PR bodies, commit messages, lint logs, etc.) to **`/tmp/claude/`** rather than `/tmp/` directly — the folder is allowlisted in `.claude/settings.json` for `Read`, `Edit`, and the common `Bash` write patterns, so writes there don't trigger a permission prompt. Pick a filename that disambiguates across concurrent uses (e.g. `/tmp/claude/pr-body-31407.md`, not `/tmp/claude/body.md`). Run `mkdir -p /tmp/claude` once if it doesn't exist.

## C++ style

- Use `yb::Thread` instead of `std::thread`.
- In tests, use `TestThreadHolder` instead of manually managed vectors of threads.
- In tests, prefer `ASSERT_*` macros over `EXPECT_*` macros when possible.
- Keep lists of `DECLARE_xxx(yyy)` flag declarations in alphabetical order.
- Keep lists of forward declarations in alphabetical order.

## Commit messages

- Use backticks for method, class, and other code references.
- Prefer `DocDB` over `docdb` as a component prefix.
- When referencing other commits (for instance as the cause of a regression), use the form
  `<COMMIT_HASH>/<DIFF_ID>`.

## Build System

The primary build entry point is `yb_build.sh` at the repository root.
Use only `yb_build.sh` for building and running tests; do not run `ninja`, `cmake`, or test
binaries directly.

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

To build a test binary without running it:

```bash
./yb_build.sh release --target tablet-test
```

To run tests:

```bash
./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter TestLoadBalancerPreferredLeader.TestBalancingMultiPriorityWildcardLeaderPreference

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter "*Wildcard*"

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test -n 10
```

Run one test per execution; do not use a `--gtest_filter` that matches more than one test.

### Java Tests

```bash
./yb_build.sh release --java-test org.yb.client.TestYBClient

./yb_build.sh release --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```
