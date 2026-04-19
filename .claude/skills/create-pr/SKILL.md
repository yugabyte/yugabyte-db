---
name: create-pr
description: >-
  Push the current branch to the user's fork of yugabyte/yugabyte-db and open
  a cross-repo GitHub pull request. Commits pending changes, ensures a fork
  exists and is used as the push target, prompts for issue reference /
  component / title, then uses `gh pr create`. Use when the user wants to
  publish a branch for review as a GitHub PR.
---

# Create PR

Push the current branch to the user's **fork** of `yugabyte/yugabyte-db` and create a cross-repo GitHub pull request against the upstream using the `gh` CLI.

**Contributors do not have push access to `yugabyte/yugabyte-db`.** The branch must be pushed to a personal fork, and the PR is opened cross-repo from that fork into the upstream repo.

## Prerequisites

This skill requires the `gh` CLI installed and authenticated:

1. **Install `gh`**: see https://cli.github.com/.
2. **Authenticate**: `gh auth login` (or `gh auth status` to verify).
3. **Fork**: the authenticated user must have a fork of `yugabyte/yugabyte-db`. If they don't, `gh repo fork --remote=false yugabyte/yugabyte-db` creates one without mutating local remotes.

## Workflow

### Step 1: Commit pending changes

`git rebase` in Step 2 refuses to run against a dirty working copy, and `gh pr create` in Step 8 only pushes committed changes — so commit first.

Run `git status`. If there are uncommitted changes:

1. Stage the relevant files (avoid `git add -A`/`.` — pick files explicitly so secrets or stray artifacts don't get included).
2. Draft a commit message based on the changes and confirm it with the user before committing.
3. Create the commit.

If the branch already has at least one commit ahead of the base and the working copy is clean, skip to Step 2.

### Step 2: Pull latest from `yugabyte/yugabyte-db` and resolve conflicts

With the working copy clean (from Step 1), bring the branch up to date with upstream `yugabyte/yugabyte-db:master` so conflicts surface now rather than during PR review.

1. **Identify the upstream remote.** Run `git remote -v` and find the remote pointing at `yugabyte/yugabyte-db` (commonly named `upstream`; sometimes `origin` in non-fork-based checkouts). If none exists, add one:
   ```
   git remote add upstream https://github.com/yugabyte/yugabyte-db.git
   ```

2. **Fetch upstream master:**
   ```
   git fetch <upstream-remote> master
   ```

3. **Rebase the branch onto upstream master:**
   ```
   git rebase <upstream-remote>/master
   ```

4. **Resolve any conflicts** with the user. For each conflict: show the conflicted files, help the user resolve them, `git add` the resolved files, and run `git rebase --continue`. If the user wants to bail out, `git rebase --abort` returns the branch to its pre-rebase state.

### Step 3: Base branch

The PR always targets `master`. Do **not** prompt the user for a base branch and do not offer alternatives. Use `master` as `--base` in Step 8 (the `gh pr create` step).

### Step 4: Identify (or create) the fork and its remote

Contributors do **not** have push access to `yugabyte/yugabyte-db`, so the branch must be pushed to a personal fork.

1. **Inspect existing remotes** with `git remote -v`. Identify which remote (if any) points at a fork (any GitHub repo that is not `yugabyte/yugabyte-db`). Common setups:
   - `origin` = `yugabyte/yugabyte-db`, `fork` or `<username>` = the personal fork
   - `origin` = the personal fork, `upstream` = `yugabyte/yugabyte-db`
2. **If a fork remote is already configured**, record its name and use it as the push target in Step 5.
3. **If no fork remote exists**, check whether the user has a fork on GitHub:
   ```
   gh repo view <username>/yugabyte-db --json parent -q .parent.nameWithOwner
   ```
   If it returns `yugabyte/yugabyte-db`, the fork exists — add it as a remote named `fork`:
   ```
   git remote add fork git@github.com:<username>/yugabyte-db.git
   ```
4. **If no fork exists at all**, create one (ask the user to confirm first):
   ```
   gh repo fork --remote=false yugabyte/yugabyte-db
   ```
   Then add the resulting fork as a `fork` remote as above.
5. **Never push to a remote that points at `yugabyte/yugabyte-db`.** Verify the chosen push target is a fork before continuing.

### Step 5: Push the branch to the fork

Push the current branch to the fork remote identified in Step 4:

```
git push -u <fork-remote> HEAD
```

If the push is rejected because the branch already exists on the fork with different history, ask the user how to proceed — do **not** force-push without explicit permission. If the user authorizes a force push, use `git push --force-with-lease` rather than `--force`.

### Step 6: Gather PR metadata

Prompt the user for the following, one at a time:

1. **Issue tracker reference** — before prompting, scan the current conversation history for any issue reference the user has already mentioned (GitHub issue numbers like `#31151`, JIRA keys like `PLAT-20518`, or URLs pointing at either). If you find a candidate, surface it to the user and ask them to confirm it's the one to use *before* asking them to supply a fresh one. Only prompt for a new reference if no candidate is found or the user rejects the candidate. Acceptable forms are:
   - A GitHub issue number (e.g., `#31151`)
   - A JIRA ticket (e.g., `PLAT-20518`)
   - Offer to **auto-create** a GitHub issue or JIRA ticket if the user doesn't have one yet. If they accept:
     - For GitHub: before calling `gh issue create`, pick a matching issue template from `.github/ISSUE_TEMPLATE/` based on the component (e.g., `docDB.yml` for DocDB, `ysql.yml` for YSQL, `ycql.yml` for YCQL, `cdc.yml` for CDC, `ui.yml` for YBA/UI, `yugabyted.yml` for yugabyted, `docs.yml` for docs, `feature_request.yml` as the generic fallback for tooling/other). Read the chosen template YAML, then (a) collect its `labels` and pass them via `--label`, (b) construct a markdown body mirroring the template's `body` sections (e.g., `### Description` textarea → `## Description` + user-facing content; `Issue Type` dropdown → pick one of the listed `options`; sensitivity checkbox → include a confirming line). Run `gh issue create --assignee @me --repo yugabyte/yugabyte-db --title <...> --body-file <path> --label <labels>`. Confirm the title/body with the user before creating. Always assign the issue to the current user.
     - For JIRA: ask the user which project (e.g., `PLAT`) and use the Atlassian MCP tool `createJiraIssue` to create it. Confirm the summary/description with the user before creating.
   - Capture the resulting issue number or JIRA key.

2. **Component** — the component tag that will appear in the title (e.g., `DocDB`, `YSQL`, `YBA`, `CDC`, `xCluster`). If the user is unsure, suggest a component based on the files changed (e.g., `src/yb/` → `DocDB`, `src/postgres/` → `YSQL`, `managed/` → `YBA`).

3. **Title** — a short description of the change. If the user doesn't provide one, propose a title based on the commit messages on the branch and confirm it with the user.

### Step 7: Build the title

Construct the title in the format:

```
[<#GH issue> or <JIRA>] <Component>: <Title>
```

Examples:
- `[#31151] DocDB: Fix SamplingProfilerTest.EstimatedBytesAndCount in release`
- `[PLAT-20518] YBA: Fix full move edit universe to populate userIntentOverrides during configure call`

### Step 8: Create the PR with `gh pr create`

Run `gh pr create` against `yugabyte/yugabyte-db`, passing the constructed title and a body derived from the branch commits. A typical invocation:

```
gh pr create \
  --repo yugabyte/yugabyte-db \
  --base <base-branch> \
  --title "<constructed-title>" \
  --body-file <path>
```

Pre-fill the body file with:

- **Summary**: a short description of the change derived from branch commits.
- **Test plan**: ask the user for one if not obvious from the branch.

Confirm the title and body with the user before running `gh pr create`.

### Step 9: Report back to the user

Output:

- The PR URL (printed by `gh pr create`)
- The constructed title
- The base branch the PR targets

## Notes

- **Never push to `yugabyte/yugabyte-db`.** Contributors do not have write access — always push to a personal fork and open the PR cross-repo.
- The title format is strict: `[<issue>] <Component>: <Title>`. Don't deviate.
- Never force-push without explicit user permission; when authorized, prefer `--force-with-lease`.
- CI runs automatically on GitHub PRs, so there is no `trigger jenkins` step (unlike the Phorge `create-review` skill).
- `gh pr create --repo yugabyte/yugabyte-db` opens the PR in the upstream repo even when the branch lives on a fork — the `head:` field is inferred from the tracking branch.
