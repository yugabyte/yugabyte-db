---
name: stack
description: >-
  This repo's rules for stacked PRs (`feature-stack/<feature>/<change>` branches), on top of the
  upstream gh-stack skill.  Use for any stack operation -- creating layers, opening their PRs,
  updating a layer, pushing, rebasing or syncing onto a newer master, checking status, and merging
  or landing a stack -- and whenever a `feature-stack/` branch is checked out.
---

# Stack

A stack is for a feature that splits into several dependent changes.  Reach for one only when the
user asks for it; a single PR is the default.

## Setup

**Load the `gh-stack` skill** and follow it for everything this skill does not override.  If it is
missing, install it at user scope for the agent you are running in:

```
gh skill install github/gh-stack gh-stack --scope user --agent <agent>
```

- **Always pass `--agent`** -- `claude-code`, `cursor`, `codex`, and so on (`gh skill install
  --help` lists the values).  Run without a terminal, `gh skill install` does not prompt and
  installs for GitHub Copilot, where other agents never see it.
- **`gh skill` needs `gh` 2.90 or newer.**  If `gh skill` is an unknown command, upgrade `gh`
  first.
- **A new skill may load only in a new session.**  Until then, read the installed `SKILL.md`
  directly; the install prints where it put it.

**The remote is the one pointing at `yugabyte/yugabyte-db`** (`git remote -v`), never your fork.
Below, `<upstream>` names it.  Pass `--remote <upstream>` wherever a `gh stack` command takes it.
Commands without the flag (`checkout`, `trunk`) pick a remote from git config and fail when a clone
has several remotes, so set this once per clone:
```
git config gh-stack.remote <upstream>
```

Only `gh stack` reads `gh-stack.remote`, so plain `git push` is unaffected.  A branch's own
`branch.<name>.remote` takes precedence over it; stack branches normally have none.

## Rules

- **Name every layer `feature-stack/<feature-name>/<change-name>`.**  That prefix is the only one
  the rulesets exempt from `Block Creations`, `Require PR`, and `yb-required`, and stack branches
  must live in `yugabyte/yugabyte-db` because GitHub has no cross-fork stacks.  `git-push.sh` warns
  on a name that isn't this two-segment shape but still pushes.
- **Push with `.agents/scripts/git-push.sh`, never `gh stack push`, `gh stack submit`, or a
  `gh stack sync` that has something to push.**  For a stack branch the script lints the whole
  stack from the top layer, then runs `gh stack push`.
- **History may be rewritten.**  The no-rewrite rule in [create-pr's Pushing follow-up
  commits](../create-pr/SKILL.md#pushing-follow-up-commits) does not apply: GitHub merges a stack
  only when its history is linear, so cascading rebases and lease-protected force-pushes are the
  normal update, even mid-review.  Never `git merge` the trunk or another layer into a stack
  branch.
- **Never `gh pr merge` a PR that is in a GitHub stack**; use `gh stack merge` (see
  [Landing](#landing)).  `gh pr edit` is broken on this repo; see [Updating PR titles and
  bodies](#updating-pr-titles-and-bodies).
- **Never click "Update branch" on a stack PR.**  Its default merges the base into the layer, which
  breaks the linear history a stack needs.  GitHub's "Rebase Stack" button is fine; see
  [Syncing](#syncing).
- **Work a stack from one clone.**  `gh stack push` fetches just before its lease, so it silently
  overwrites commits that reached a layer from anywhere but this clone.  Worktrees of the same
  clone share branches and are safe, though `gh stack rebase` stops if a worktree holding a layer
  has uncommitted changes.

## Opening PRs

Open each layer's PR with the `create-pr` skill (`create-pr.sh`), not `gh stack submit`, one layer
at a time from the bottom up, because `submit`'s generated titles fail the pr-title check.  Don't
pass `-b`; the script takes the base from `gh stack`.  It opens the PR as a draft, links it into the
stack with `gh stack link`, and then marks it ready unless you passed `-D`.  The bottom layer's PR
is linked when the next layer's PR opens, since `gh stack link` needs at least two PRs.

## Updating a layer

Commit on the layer that owns the change, never on a higher one, then replay the layers above it
and push:
```
gh stack checkout <layer>        # or up / down / top / bottom
git add <files> && git commit -F <message-file>
gh stack rebase --upstack --remote <upstream>
.agents/scripts/git-push.sh
```

Then report which PRs need a new Jenkins run (see [Jenkins](#jenkins)), and check the changed
layer's PR title and body (see [Updating PR titles and bodies](#updating-pr-titles-and-bodies)).

## Syncing

To pick up a newer `master`, after a lower layer merged, or when the user asks to rebase or sync
the stack:

```
gh stack rebase --remote <upstream>
.agents/scripts/git-push.sh
gh stack sync --remote <upstream> --prune
```

- `rebase` replays past a squash-merged layer with `--onto`, so a layer merged in the GitHub UI
  does not cause spurious conflicts.  On a conflict (exit 3), resolve, `git add`, then
  `gh stack rebase --continue`, or `--abort` to restore every branch.
- `sync` runs last only for bookkeeping: PR state, the stack link on GitHub, and deleting merged
  layers' local branches.  By then there is nothing left to push.  If `master` moves in between,
  `sync` rebases and pushes again without lint; that is rare and CI's `pr-lint` still checks each
  layer.
- `--prune` keeps a merged branch that another worktree has checked out.  If the current worktree
  is on a merged branch, it first switches to the lowest unmerged layer, rewriting files (and so
  triggering C++ rebuilds); run it from an unmerged layer, such as the top, to avoid that.
- After GitHub's "Rebase Stack" button, or GitHub's own rebase of the remaining layers after a
  partial merge, the local layers have diverged from GitHub's.  The same three commands replace
  GitHub's rebase with the equivalent local one; nothing is lost, but every layer gets new commits.


## Jenkins

Every push that changes a layer's head commit needs a new Jenkins run on that PR before the stack
can merge.  After such a push, list the affected PRs; run `.agents/scripts/trigger-jenkins.sh <pr>`
only when the user says to.


## Updating PR titles and bodies

After a push that changes what a layer contains, check that layer's PR title and body against the
policy in `src/AGENTS.md` ("Keep the PR summary in sync" and "Keep the PR title in sync"), and
apply any update with the REST commands in `.agents/docs/pr-metadata-sync.md`.  `git-push.sh`
prints no such reminder for a stack branch, so check without one.  A rebase alone changes no
layer's content and needs no update.


## Status

```
gh stack view --json
gh pr view <pr> --json isDraft,reviewDecision,headRefOid,mergeStateStatus
gh pr checks <pr>
```

`needsRebase` in the view means the layer no longer contains its parent's tip.  It does not by
itself block a merge; GitHub's merge box decides.


## Landing

1. **Check every layer up to the target**: open, not a draft, approved, and all checks green,
   including Jenkins on its current head commit.  If any fails, stop and report it.
2. **Merge** the target and everything below it in one all-or-nothing operation:

   ```
   gh stack merge <top-pr> --squash --yes
   ```

   Squash is the only method the repo allows.  The merge box's "Squash and merge stack" button
   does the same thing.  If GitHub refuses because the stack is not linear, follow
   [Syncing](#syncing), wait for Jenkins, and retry.

   A PR that is not yet in a GitHub stack -- the only PR so far, since a GitHub stack needs at
   least two -- makes `gh stack merge` fail with "not a stack number or a stacked pull request".
   Check with `gh api repos/yugabyte/yugabyte-db/pulls/<pr> --jq .stack`, which prints `null` for
   such a PR, and land it as an ordinary PR with `gh pr merge <pr> --squash`.
3. **Clean up** with `gh stack sync --remote <upstream> --prune`.
