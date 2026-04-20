---
name: create-review
description: >-
  Create a YugabyteDB Phorge review (diff) for the current branch's changes.
  Use when the user wants to publish their changes for review.
allowed-tools: Bash(git status:*), Bash(git log:*), Bash(git diff:*), Bash(git add:*), Bash(git remote:*), Bash(git show:*), Bash(arc diff:*), Bash(./build-support/lint.sh:*), Read
---

# Create Review

Create a Phorge review for the current branch using `arc diff --create`.

## Prerequisites

This skill requires `arc` (Arcanist) to be installed and configured for the YugabyteDB Phorge instance. If `arc` is not available, instruct the user to set it up.

## Workflow

### Step 1: Commit pending changes

`arc diff --create` requires a clean working copy — any unstaged or untracked changes on the branch must be committed first.

Run `git status`. If there are uncommitted changes:

1. Stage the relevant files (avoid `git add -A`/`.` — pick files explicitly so secrets or stray artifacts don't get included).
2. Draft a commit message based on the changes and confirm it with the user before committing.
3. Create the commit.

If the branch already has at least one commit and the working copy is clean, skip to Step 2.

### Step 2: Run linter and confirm no lint errors

Run `./build-support/lint.sh` from the repo root. Report the output to the user.

- If there are **errors**, stop and fix the issues before continuing. Do not proceed to `arc diff --create`.
- If there are only **warnings or advice**, show them to the user and ask whether to proceed.
- If the output is clean, continue to the next step.

### Step 3: Gather title metadata

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

### Step 4: Determine subscribers

Based on the files changed on the branch, decide the default subscriber:

- If changes are in DB code (`src/`, `java/`, `cmake_modules/`, `build-support/`, etc.) → add `ybase` as a subscriber.
- If changes are in YBA / platform code (`managed/`) → add `yugaware` as a subscriber.
- If changes span both, add both `ybase` and `yugaware`.

Confirm the subscriber list with the user before creating the diff.

### Step 5: Build the title

Construct the title in the format:

```
[<#GH issue> or <JIRA>] <Component>: <Title>
```

Examples:
- `[#31151] DocDB: Fix SamplingProfilerTest.EstimatedBytesAndCount in release`
- `[PLAT-20518] YBA: Fix full move edit universe to populate userIntentOverrides during configure call`

### Step 6: Create the diff with `arc diff --create`

Run `arc diff --create` **without** using the Phorge MCP server. Do **not** amend the user's existing commit to change its title — always pass the Phorge revision title and body via `--message-file` so the local commit stays untouched.

A typical invocation:

```
arc diff --create --message-file <path> --reviewers <reviewers> --cc <subscribers> <base>
```

Pre-fill the message file with:

- **Title**: the constructed title from Step 4
- **Summary**: a short description of the change (derive from branch commits)
- **Test Plan**: ask the user for one if not obvious from the branch
- **Reviewers**: (as provided)
- **Subscribers**: `ybase` and/or `yugaware` per Step 4

Show the user the resulting diff URL (e.g., `https://phorge.dev.yugabyte.com/D<id>`).

### Step 7: Post a "trigger jenkins" comment

After the diff is created, post a comment on the new revision with the exact text:

```
trigger jenkins
```

Use `arc` to post the comment (do not use the Phorge MCP server). For example:

```
echo "trigger jenkins" | arc call-conduit differential.createcomment -- '{"revision_id": "<id>", "message": "trigger jenkins"}'
```

Or use whatever `arc` subcommand is appropriate for the local setup. Confirm the comment was posted successfully.

### Step 8: Report back to the user

Output:

- The diff URL
- The constructed title
- The subscribers that were added
- Confirmation that `trigger jenkins` was posted

## Notes

- Never use the Phorge MCP server for creating the diff — use `arc` CLI only.
- Always run `./build-support/lint.sh` first; a failing lint should block diff creation.
- The title format is strict: `[<issue>] <Component>: <Title>`. Don't deviate.
- `ybase` for DB, `yugaware` for YBA/platform — add both when changes span both.
- The `trigger jenkins` comment kicks off CI; forgetting it is a common mistake.
