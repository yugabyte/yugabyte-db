## Making changes and pushing to upstream
Never operate on master branch directly. Always create a new local branch and work on that.
Pushing the branch to yugabyte/yugabyte-db repository is not allowed. If you are on a personal fork, you can push to that.
arc and phorge are used to review, run lab tests, and merge changes. This should ONLY be done by the human.

Avoid using non-ASCII characters in files and commit messages.
There may be some exceptions where appropriate such as `collate.icu.utf8.sql` and `jsonpath_encoding.out`.

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
