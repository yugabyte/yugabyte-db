---
description: Backport a Phorge-landed commit (or revision) to one or more YugabyteDB release branches as Phorge/arc revisions
argument-hint: <commit-sha|Dxxxxx> [<branch> ...]
allowed-tools: Bash(backport.sh *)
model: sonnet
---

Backport a change that landed through Phorge to one or more YugabyteDB release branches using `backport.sh`. It cherry-picks (or `arc patch`es, for a `Dxxxxx` argument) onto each release branch, rewrites the message to `[BACKPORT <branch>] …` with an `Original commit: <sha> / <Dxxxxx>` line in the Summary, and publishes each branch with `arc diff -- origin/<branch>`. Wrap it with conflict-resolution logic: trivial whitespace conflicts are fixed automatically; anything more complex is escalated to the user.

**This command produces Phorge/arc revisions.** A backport should go through the same review system the original went through, so if the original landed as a GitHub PR — subject ends `(#NNNNN)`, committer is `GitHub <noreply@github.com>`, and the body has no `Differential Revision:` line — stop and use `/backport-commit` instead. If neither pattern matches cleanly, ask the user rather than guessing.

This command is self-contained by design: it shares no content with `/backport-commit`, so once YugabyteDB finishes moving to GitHub PRs, deleting this file is the whole cleanup.

**Always pass the full branch list in a single invocation.** After each successful branch, the script chains the next branch's cherry-pick from the previous branch's task branch — so a conflict you resolve on `b2` carries forward, and `b3+` typically apply cleanly without re-resolving the same conflict.

## Inputs

Parse `$ARGUMENTS` (whitespace-separated):
- `<commit>` = first token. Either a git SHA (>=7 hex chars, must be merged to `master` and reachable from `origin`) or a Phorge revision ID (`D` followed by digits).
- `<branches>` = remaining tokens. **Optional.**

If `<commit>` is missing, stop and ask the user.

If no `<branches>` are provided, ask the user which release branches to target before invoking the script. Do **not** rely on the script's interactive prompt mode — Claude Code's Bash tool cannot answer interactive `read -p` prompts and the run will hang. Active stable branches: `curl -s https://release.dev.yugabyte.com/version/active/text`. Backport to the branches that actually carry the affected code.

```
$ARGUMENTS
```

## Prerequisites

- `arc` on `PATH` and authenticated (`arc call-conduit -- user.whoami`).
- `backport.sh` on `PATH`. Unlike `backport-commit.sh`, it is **not** shipped in this repo — it's a separate internal tool. If `which backport.sh` comes up empty, say so and stop rather than falling back to `/backport-commit`.
- A clean backport workspace. The script clones/reuses `$YB_BACKPORT`, defaulting to `$HOME/code/backport-ybdb`; it is a **separate clone**, not the current worktree, so the current worktree's state is irrelevant. It aborts with exit `2` if that workspace is dirty — do **not** stash or `git checkout -- .` to clear it without confirming with the user; those changes may be in-progress work.

## Workflow

### Step 1: Run the backport script with all branches at once

```
EDITOR=true GIT_EDITOR=true backport.sh <commit|Dxxxxx> <branch1> <branch2> ... </dev/null 2>&1 | tee /tmp/claude/backport.log
```

Every part of that invocation matters:
- `EDITOR=true GIT_EDITOR=true` — **required**, or `arc diff` opens vim and the run hangs forever.
- `</dev/null` — the script falls back to `read -p` prompts (unknown revision ID, per-branch confirmation when no branches are given); redirecting stdin makes those fail fast instead of hanging.
- Explicit branches — with none given, the script prompts per stable branch.

Exit codes:
- `0` — all branches succeeded. Revision URLs are the `https://phorge.dev.yugabyte.com/D...` lines `arc diff` prints, one per branch.
- `2` — either the workspace was dirty (message: `Workspace ... is not clean`) or a cherry-pick hit conflicts (message: `Backport ERROR: Merge conflicts?` plus the workspace path, task branch `backport-<id>-<branch>`, and a suggested rerun line). Distinguish the two before acting.
- other — pre-flight failure (malformed ID, unknown commit, clone failure). Report to the user.

If the script warns `Could not find a Differential Revision ID in commit log` (or, for a `Dxxxxx` argument, `Could not find a landed commit ID in phabricator`), it wanted to prompt for a value and got EOF from `</dev/null`. That means the original's cross-reference is missing — ask the user what should go in the `Original commit:` line rather than letting it record `None`.

### Step 2: Resolve conflicts (only if exit code 2 with a merge-conflict message)

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

2. `git cherry-pick --continue --no-edit` to land the resolved commit. The script strips the stale `Differential Revision:` line from `.git/MERGE_MSG` for you before exiting, so `--continue --no-edit` won't carry the original revision's tag onto the backport commit — don't re-add it. If the resolved diff is empty, run `git cherry-pick --skip` instead.

3. **Track every resolution, trivial or not**, so a reviewer can see what was changed vs. the original commit. Record `<path>:<line>` and a short summary of the resolution. For trivial cases a one-liner is fine ("accepted cherry-pick's added block; branch had no code at that location"); for non-trivial cases, expand to one line per hunk you reasoned about. May be multiple lines per file. Also remember **which branch** the resolution was on.

4. **Record those notes in the commit message**, not in a post-hoc comment — there is no Phorge equivalent of a GitHub PR body patch that doesn't risk an `arc diff --update` re-upload. Amend the body to add a `Merge conflicts:` block **inside the Summary section**, above the `Reviewers:`/`Subscribers:` fields. Free text placed after `Subscribers:` makes `arc diff` hang on a prompt. The script's own `git commit --amend` preserves the rest of the body verbatim, so the block survives into the published revision's summary. For example, the Summary section becomes:

   ```
   Summary:
   Original commit: abc1234 / D50001
   <original summary text>

   Merge conflicts:
   - src/yb/master/catalog_manager.cc:1843 — kept master's call signature; release branch lost the `epoch` arg
   - src/yb/master/catalog_manager.h:412 — moved the new method below the existing private block
   ```

   Annotate **every** branch where you ran `git cherry-pick --continue`, trivial or not. "Trivial" describes how easy the resolution was, not whether it warrants disclosure. Branches that picked up a resolved diff via chaining need no annotation — that's already implicit in the branch they chained from.

5. **Run the linter** from the repo root and confirm it is clean before re-running:
   ```
   ./build-support/lint.sh --rev origin/<release-branch>
   ```
   Pass `--rev` explicitly: a backport task branch has no `@{upstream}`, and `lint.py`'s fallback base is a guess at best. If the linter reports errors, fix them, `git add` the fixes, and `git commit --amend --no-edit`. Do not proceed until lint output is clean.

6. Re-run the script with `-x <conflicted-branch>` and the **same full branch list** as the original invocation:
   ```
   EDITOR=true GIT_EDITOR=true backport.sh -x <conflicted-branch> <commit|Dxxxxx> <branch1> <branch2> ... </dev/null
   ```
   `-x` tells the script to accept the current commit on that task branch as the resolved change instead of re-applying it. Branches whose task branch already carries a `Differential Revision:` line are skipped (arc already posted them), so re-runs don't create duplicate revisions.

If a *later* branch in the same run also hits conflicts, repeat this step for that branch (with a fresh `-x <new-conflicted-branch>`). Continue until the script exits `0`.

### Step 3: Capture revision URLs

Collect every `https://phorge.dev.yugabyte.com/D...` URL printed by `arc diff` across the (possibly multi-run) output. Each successfully backported branch produces one revision. If the output scrolled past, find them by task branch:

```
cd <workspace> && git log -n1 --format=%b backport-<id>-<branch> | grep 'Differential Revision:'
```

### Step 4: CI

`arc diff` on a newly created revision **auto-fires** the Harbormaster builds, including `DB Builds/UnitTests (Trigger: db)`. Do **not** post an extra `trigger jenkins` comment — it dedups per diff-version and does nothing when a DB build already exists for the current diff. The DB build then stays `building` for hours until Detective reports back; success shows up as a `Diff ID: <diffid> Passed test criteria` comment on the revision.

Only if a revision has no DB build at all should you post one:

```
echo '{"revision_id":<n>,"message":"trigger jenkins"}' | arc call-conduit differential.createcomment --
```

Check for an existing build first (`harbormaster.buildable.search` constrained on the revision PHID, then `harbormaster.build.search` on the buildable), or use the `phabricator_buildable_search` MCP tool.

### Step 5: Report back to the user

Output a single summary listing, per branch:
- The revision URL.
- Whether merge conflicts were resolved on that branch (and trivial vs. non-trivial), or whether it chained cleanly from a resolved predecessor.
- Any branch that was skipped or aborted, and why.

## Notes

- The script keeps the original reviewers by default. Pass `-r "<names>"` only if the user asks for a different list.
- `master` is silently skipped if passed as a branch — that's expected.
- Do **not** pass `-n` (preview mode) unless the user explicitly asks for a dry run; the command is meant to actually publish revisions.
- Do **not** run a manual `arc diff --update` against a backport revision as part of this workflow. If you ever must, it needs `-m "msg"` and `--base 'git:<parent-sha>'` (otherwise arc computes the base against `master` and the diff balloons to every master↔release difference), and `--verbatim` is incompatible with `--update`.
- Landing these revisions is a separate step and is **not** part of this command. Don't `arc land` without being asked.
