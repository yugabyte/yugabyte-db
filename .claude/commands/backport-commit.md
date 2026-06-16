---
description: Backport a commit to one or more YugabyteDB release branches
argument-hint: <commit-sha> [<branch> ...]
allowed-tools: Bash(.agents/scripts/backport-commit.sh *)
model: sonnet
---

Backport a merged commit to one or more YugabyteDB release branches using the bundled `backport-commit.sh` at `.agents/scripts/backport-commit.sh`. The script handles cherry-picking, message rewriting, fork detection, push, and PR creation. Wrap it with conflict-resolution logic: trivial whitespace conflicts are fixed automatically; anything more complex is escalated to the user.

**Always pass the full branch list in a single invocation.** After each successful branch, the script chains the next branch's cherry-pick from the previous branch's task branch — so a conflict you resolve on `b2` carries forward, and `b3+` typically apply cleanly without re-resolving the same conflict.

## Inputs

Parse `$ARGUMENTS` (whitespace-separated):
- `<commit>` = first token (>=7 hex chars, must be merged to `master` and reachable from `origin`).
- `<branches>` = remaining tokens. **Optional.**

If `<commit>` is missing, stop and ask the user.

If no `<branches>` are provided, ask the user which release branches to target before invoking the script. Do **not** rely on the script's interactive prompt mode — Claude Code's Bash tool cannot answer interactive `read -p` prompts and the run will hang.

```
$ARGUMENTS
```

## Prerequisites

- `gh` CLI installed and authenticated (`gh auth status`).
- A personal fork of `yugabyte/yugabyte-db` (the script auto-detects it from the authenticated `gh` user).

## Workflow

### Step 1: Run the backport script with all branches at once

```
.agents/scripts/backport-commit.sh <commit> <branch1> <branch2> ...
```

Tee the output for inspection:

```
.agents/scripts/backport-commit.sh <commit> <branch1> <branch2> ... 2>&1 | tee /tmp/claude/backport.log
```

Exit codes:
- `0` — all branches succeeded. PR URLs are the `https://github.com/...` lines in the output, one per branch.
- `2` — cherry-pick hit conflicts on one branch; the script prints the workspace path, the conflicted task branch (`backport-<id>-<branch>`), and a suggested rerun line, then exits.
- other — pre-flight failure (dirty workspace, unknown commit, missing `gh`, etc.). Report to the user.

### Step 2: Resolve conflicts (only if exit code 2)

The script's failure message names the workspace and the conflicted task branch. `cd` to that workspace and run `git status` to list conflicted files.

For **each** conflicted file, run `git diff` (and, if useful, `git diff --check` for whitespace-only markers) and classify the conflict:

- **Trivial — resolve automatically without asking:**
  - Pure whitespace differences (tabs vs. spaces, trailing whitespace, indentation only).
  - Line-ending differences.
  - Conflicts where both sides are byte-identical after whitespace normalization.
  - Adjacent-but-non-overlapping hunks that git could have merged with a wider context window.

- **Non-trivial — stop and ask the user:**
  - Any logic, identifier, or signature change on either side.
  - Code that has been refactored, renamed, moved, or restructured between `master` and the release branch.
  - Anything where choosing a side requires understanding intent.

  Show the user the conflicted file(s) and a summary of the conflict, then ask whether you should attempt the merge or whether they want to take over. Do not guess.

After resolving each file:
1. `git add <file>` — stage the resolution.

Once **all** conflicts are resolved (and only then):

2. `git cherry-pick --continue --no-edit` to land the resolved commit. Use `--no-edit` so the original commit message is preserved verbatim — the script re-derives the subject/body from `$commitid` on rerun, so anything you type here gets discarded anyway. If the resolved diff is empty, run `git cherry-pick --skip` instead.

3. **Track every resolution, trivial or not.** Any conflict you touched by hand needs a paper trail in Step 4 so a reviewer can see what was changed vs. the original commit. Record `<path>:<line>` and a short summary of the resolution. For trivial cases a one-liner is fine ("accepted cherry-pick's added block; branch had no code at that location"); for non-trivial cases, expand to one line per hunk you reasoned about. May be multiple lines per file. Also remember **which branch** the resolution was on. Example:
   ```
   2024.2 (non-trivial):
     src/yb/master/catalog_manager.cc:1843 — kept master's call signature; release branch lost the `epoch` arg
     src/yb/master/catalog_manager.cc:2017 — re-applied the cherry-picked guard around the older AddTask path
     src/yb/master/catalog_manager.h:412 — moved the new method below the existing private block

   2025.2 (trivial):
     src/yb/master/master-path-handlers.cc:1015 — accepted cherry-pick's added block; branch had no code at that location
   ```

4. **Run the linter** from the repo root and confirm it is clean before re-running:
   ```
   ./build-support/lint.sh
   ```
   If the linter reports errors, fix them, `git add` the fixes, and `git commit --amend --no-edit`. Do not proceed until lint output is clean.

5. Re-run the script with `-x <conflicted-branch>` and the **same full branch list** as the original invocation:
   ```
   .agents/scripts/backport-commit.sh -x <conflicted-branch> <commit> <branch1> <branch2> ...
   ```
   The script will skip branches that already have backport PRs, apply `-x` to skip the cherry-pick on the conflicted branch (using your resolved commit), then continue with the remaining branches — chaining their cherry-picks from the resolved task branch so they typically apply cleanly without re-resolving.

If a *later* branch in the same run also hits conflicts, repeat this step for that branch (with a fresh `-x <new-conflicted-branch>`). Continue until the script exits `0`.

### Step 3: Capture PR URLs

Extract every `https://github.com/...` URL printed by `gh pr create` from the (possibly multi-run) script output. Each successfully backported branch produces one PR.

### Step 4: Annotate the PR body for every branch where a conflict was resolved by hand

Any branch where you ran `git cherry-pick --continue` (vs. it auto-applying cleanly) needs an annotation, **regardless of whether the resolution was trivial or non-trivial**. Reviewers shouldn't have to diff the backport against the original commit to discover there was a manual step. "Trivial" describes how easy the resolution was, not whether it warrants disclosure.

Branches that picked up a resolved diff via chaining don't need annotation — that's already implicit in the upstream branch they chained from.

For each such PR:

```
gh pr view <pr-number> -R yugabyte/yugabyte-db --json body -q .body > /tmp/claude/pr-body-<pr-number>.md
```

Append a `## Merge conflicts` section using the entries you recorded in Step 2 (substep 3) for that branch. For example:

```
## Merge conflicts

- `src/yb/master/catalog_manager.cc:1843` — kept master's call signature; release branch lost the `epoch` arg
- `src/yb/master/catalog_manager.cc:2017` — re-applied the cherry-picked guard around the older AddTask path
- `src/yb/master/catalog_manager.h:412` — moved the new method below the existing private block
```

Then push the new body via the REST API. **Do not use `gh pr edit --body-file`** — it errors with `GraphQL: Projects (classic) is being deprecated... (repository.pullRequest.projectCards)` on this repo:

```
jq -Rs '{body: .}' < /tmp/claude/pr-body-<pr-number>.md |
  gh api -X PATCH /repos/yugabyte/yugabyte-db/pulls/<pr-number> --input -
```

### Step 5: Trigger Jenkins on every PR

Comment `Trigger Jenkins` on each PR (whether or not it had conflicts):

```
gh pr comment <pr-number> -R yugabyte/yugabyte-db --body "Trigger Jenkins"
```

### Step 6: Report back to the user

Output a single summary listing, per branch:
- The PR URL.
- Whether merge conflicts were resolved on that branch (and trivial vs. non-trivial), or whether it chained cleanly from a resolved predecessor.
- Any branch that was skipped or aborted, and why.

## Notes

- The script auto-detects reviewers from the originating PR (union of pending review requests and actual reviewers, minus bots and self) — do not pass `-r` unless the user asks for a different list.
- The script auto-links the originating PR in the backport body via the `/repos/<repo>/commits/<sha>/pulls` API; do not duplicate that link manually.
- `master` is silently skipped by the script if passed as a branch — that's expected.
- If a backport PR for `<head>=<fork-owner>:backport-<id>-<branch>` already exists, the script logs and skips it. Treat that as a no-op success for that branch.
- Do **not** bypass the script's dirty-workspace check by stashing or `git checkout -- .` without confirming with the user — those changes may be the user's in-progress work.
- Do **not** pass `-n` (preview mode) unless the user explicitly asks for a dry run; the command is meant to actually publish PRs.
