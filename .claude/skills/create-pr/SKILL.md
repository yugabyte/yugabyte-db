---
name: create-pr
description: >-
  Create a Pull Request for the current branch's changes.
  Use when the user wants to publish a branch for review as a GitHub PR.
allowed-tools: Bash(.agents/scripts/create-pr.sh *)
model: sonnet
---

# Create PR

Push the current branch to the user's **fork** of `yugabyte/yugabyte-db` and create a cross-repo GitHub pull request against the upstream using the `gh` CLI.

**Contributors do not have push access to `yugabyte/yugabyte-db`.** The branch must be pushed to a personal fork, and the PR is opened cross-repo from that fork into the upstream repo.

## Confidentiality — read before you write anything public

**The upstream repo, every GitHub issue, every PR title/description/comment, and every commit message are PUBLIC on the internet.** Anything you (the agent) write into them is permanent and indexed by search engines. A leak cannot be unsent.

**Never put any of the following into the GitHub issue (title/body/labels), the PR title, PR description, PR comments, commit messages, code comments, test code, test fixtures, golden output, or filenames:**

- **Customer-identifying data** — company names, account IDs, universe/cluster names or UUIDs, support case numbers, environment names, region/zone names tied to a customer deployment. Substitute with `customer-1`, `acme-corp`, or a generic description.
- **PII** — real names, real email addresses, real phone numbers, real postal addresses, real IP addresses (public or private). In tests use only the documentation ranges: IPv4 `192.0.2.0/24` / `198.51.100.0/24` / `203.0.113.0/24` (RFC 5737), IPv6 `2001:db8::/32` (RFC 3849), hostnames `example.com` / `example.org` / `example.net` (RFC 2606), names `Alice`/`Bob`/`Carol`, phone numbers `555-0100`–`555-0199`.
- **Unanonymized customer schema or queries** — table names, column names, SQL text, query plans, or sample rows pulled from a real customer report. Reconstruct a synthetic minimal reproducer using generic identifiers (`t1`, `users`, `id`, `value`) that demonstrates the same defect.
- **Secrets / credentials** — API keys, tokens, passwords, TLS certificates, private keys, license keys, kubeconfigs, production S3 buckets, internal Slack/JIRA/Linear URLs, internal hostnames, vault paths. Use placeholders like `AKIAIOSFODNN7EXAMPLE` in mocks.
- **YugabyteDB-internal information not yet public** — unreleased roadmap, internal SLAs, embargoed security findings, internal infra hostnames.

**This applies to the test code and test data you write too** — `.cc`, `.py`, `.java`, `.sql`, golden `.out` files, YAML fixtures, mock responses, anything. If a customer's reproducer uses `acme_orders` with a `customer_email` column populated with real addresses, you must rewrite it as `t1`/`email` with `user@example.com` before the test goes anywhere near a PR.

**When the source is a customer report:** read the original from the internal source (JIRA / support ticket / Slack) — never paste or link it from a public artifact. Reproduce locally with synthetic inputs. Land only the synthetic reproducer. Reference the issue by internal ticket ID (`PLAT-20518`) only; don't quote customer-facing text.

**If you're unsure whether a string is sensitive, don't write it down — ask the user.** A clarifying question costs nothing; a leaked customer name in a public PR cannot be retracted.

This rule applies at every step below — when you draft the commit message in Step 1, when you draft the issue body in Step 3, when you write the title in Step 4, and when you compose the description / test plan / upgrade notes in Step 5. See `src/AGENTS.md` § Confidentiality for the canonical list.

## Prerequisites

This skill requires the `gh` CLI installed and authenticated:

1. **Install `gh`**: see https://cli.github.com/.
2. **Authenticate**: `gh auth login` (or `gh auth status` to verify).
3. **Fork**: the authenticated user must have a fork of `yugabyte/yugabyte-db`. If they don't, `gh repo fork --remote=false yugabyte/yugabyte-db` creates one without mutating local remotes.

## Workflow

### Step 1: Commit pending changes

`create-pr.sh` (Step 5) refuses to run against a dirty working copy and only pushes committed changes — so commit first.

Run `git status`. If there are uncommitted changes:

1. Stage the relevant files (avoid `git add -A`/`.` — pick files explicitly so secrets or stray artifacts don't get included).
2. Draft a commit message based on the changes and confirm it with the user before committing.
3. Create the commit. **Use `git commit -F <message-file>` rather than `-m "..."` or a quoted heredoc** — apostrophes, backticks, or `$` in the message will silently break a `<<'EOF' ... EOF` heredoc with a confusing `unexpected EOF` shell error. Write the message to a temp file (e.g., `/tmp/claude/commit-msg-<issue>.txt`) and pass it via `-F`.

If the branch already has at least one commit ahead of the base and the working copy is clean, skip to Step 2.

### Step 2: Base branch

The PR targets `master`. Do **not** prompt the user for a base branch — `create-pr.sh` always rebases and pushes against `master`. Backports are not opened with this skill — use `/backport-commit` instead.

### Step 3: Gather PR metadata

Infer as much as you can from context first, then **batch-confirm with the user in a single prompt** if all three fields below are inferable with high confidence. Only fall back to per-field prompts when something is missing or ambiguous.

1. **Issue tracker reference** — before prompting, look for candidates in this order:
   1. The current branch name (`git branch --show-current`). Branches frequently embed the issue number directly: e.g. `Fix31329`, `fix-31151`, `pr/31151`, or `PLAT-20518-thing` → `#31329`, `#31151`, `PLAT-20518`. Match `#?\d{4,}` for GitHub issues and `[A-Z]+-\d+` for JIRA keys.
   2. The conversation history for any issue reference the user has mentioned (GitHub issue numbers like `#31151`, JIRA keys like `PLAT-20518`, or URLs pointing at either).
   3. Recent commit messages on the branch (`git log <upstream>/master..HEAD`).

   If you find a candidate, surface it to the user and ask them to confirm it's the one to use *before* asking them to supply a fresh one. Only prompt for a new reference if no candidate is found or the user rejects the candidate. Acceptable forms are:
   - A GitHub issue number (e.g., `#31151`)
   - A JIRA ticket (e.g., `PLAT-20518`)
   - Offer to **auto-create** a GitHub issue or JIRA ticket if the user doesn't have one yet. If they accept:
     - For GitHub: before calling `gh issue create`, pick a matching issue template from `.github/ISSUE_TEMPLATE/` based on the component (e.g., `docDB.yml` for DocDB, `ysql.yml` for YSQL, `ycql.yml` for YCQL, `cdc.yml` for CDC, `ui.yml` for YBA/UI, `yugabyted.yml` for yugabyted, `docs.yml` for docs, `feature_request.yml` as the generic fallback for tooling/other). Read the chosen template YAML, then (a) collect its `labels` and pass them via `--label`, (b) construct a markdown body mirroring the template's `body` sections (e.g., `### Description` textarea → `## Description` + user-facing content; `Issue Type` dropdown → pick one of the listed `options`; sensitivity checkbox → include a confirming line). Run `gh issue create --assignee @me --repo yugabyte/yugabyte-db --title <...> --body-file <path> --label <labels>`. Confirm the title/body with the user before creating. Always assign the issue to the current user.
     - For JIRA: ask the user which project (e.g., `PLAT`) and use the Atlassian MCP tool `createJiraIssue` to create it. Confirm the summary/description with the user before creating.
   - Capture the resulting issue number or JIRA key.

   - Don't reuse an issue across multiple in-flight master PRs — auto-create a fresh issue (per the flow above) when starting unrelated work, even if a related closed/merged PR used a similar issue.

2. **Component** — the component tag that will appear in the title (e.g., `DocDB`, `YSQL`, `YBA`, `CDC`, `xCluster`). Infer from the files changed (`git diff --name-only <upstream>/master..HEAD`):
   - `src/yb/` → `DocDB`
   - `src/postgres/` → `YSQL`
   - `src/yb/yql/cql/` → `YCQL`
   - `managed/` → `YBA`
   - `yugabyted-ui/` → `yugabyted`
   - `docs/` → `Docs`
   - `.claude/`, `AGENTS.md`, `CLAUDE.md` → `ClaudeCode`
   - `build-support/`, `cmake_modules/`, `.github/` → `Build`
   - mixed/cross-cutting → ask the user

3. **Title** — a short description of the change. If the branch has a single commit, use its subject (minus the `[<issue>] <Component>:` prefix if present). Confirm with the user.

### Step 4: Build the title

Construct the title in the format:

```
[<#GH issue> or <JIRA>] <Component>: <Title>
```

Examples:
- `[#31151] DocDB: Fix SamplingProfilerTest.EstimatedBytesAndCount in release`
- `[PLAT-20518] YBA: Fix full move edit universe to populate userIntentOverrides during configure call`

**Pass only the `<Component>: <Title>` part to `create-pr.sh -t`** — the script prepends `[<issue>] ` from `-i` automatically. The script validates both inputs and rejects invalid ones with `exit 1`:

- `-i`: must be a GH issue (`#NNNN` or bare `NNNN`) or a JIRA key (`PROJECT-NNN`, where `PROJECT` is uppercase letters). A **comma-separated list** is also accepted (e.g. `31151, #31152, PLAT-333`) — the script joins valid tokens with `, ` and renders them as `[#a, #b, PLAT-c] <Component>: <Title>`. Anything else is rejected.
- `-t`: must match `^[A-Za-z][A-Za-z0-9]+:[[:space:]].+` — i.e. a Component prefix (letter-led, alphanumeric, ≥2 chars), then `: `, then a non-empty description. Known components: `DocDB`, `YSQL`, `YCQL`, `YBA`, `CDC`, `xCluster`, `yugabyted`, `Docs`, `ClaudeCode`, `Build`.

If the user supplies a title that already includes `[<issue>] ` or doesn't match the format, fix it before calling the script — don't rely on the script's auto-strip (it only handles the leading `[*] ` case) and don't surprise the user with an `exit 1` after they confirmed the metadata.

### Step 5: Run `create-pr.sh` to rebase, lint, push, and open the PR

> **Confidentiality — final scrub before publishing.** The repo and every PR are public. Before invoking the script, re-read the description, test plan, upgrade-rollback notes, the commit messages on the branch, **and the test code being added**, and confirm none of the following appear: customer names or identifiers (universe UUIDs, account IDs, support cases, environment names, region/zone names); PII (real names / emails / phone numbers / postal addresses / IP addresses — use RFC 5737/3849 documentation ranges in tests); unanonymized customer schemas (table / column / query text / query plans / sample rows from a real customer — reconstruct a synthetic reproducer); credentials, tokens, certificates, private keys, or license keys; internal-only hostnames, URLs, Grafana/Slack/Linear links, or vault paths; unreleased internal information (roadmap, SLAs, embargoed security findings, internal infra hostnames). See the top of this skill and `src/AGENTS.md` § Confidentiality for the full rule. If unsure whether a string is sensitive, don't write it down — ask the user.

Once you have the issue (Step 3.1), title (Step 4), and reviewers (Step 3 if user-supplied), write the description and test plan to **separate** temp files and hand everything to the script:

```
.agents/scripts/create-pr.sh \
  -i <issue> \
  -t "<Component>: <Title>" \
  -d /tmp/claude/pr-desc-<issue>.md \
  -T /tmp/claude/pr-testplan-<issue>.md \
  [-U /tmp/claude/pr-upgrade-<issue>.md] \
  [-D] \
  -r <reviewers>
```

The script rebases on `<upstream>/master`, runs `lint.sh --rev <upstream>/master` and refuses to push if it isn't clean, pushes to your fork, assembles the PR body as `## Summary` (from `-d`) followed by `## Test plan` (from `-T`), runs `gh pr create`, and adds reviewers via the REST `requested_reviewers` endpoint (which correctly routes user logins to `reviewers[]` and team slugs to `team_reviewers[]`). It auto-detects the upstream and fork remotes.

Inputs:
- **`-i`**: bare GH number (`31151`), `#`-prefixed (`#31151`), or a JIRA key (`PLAT-20518`). Pass a **comma-separated list** to track multiple issues in one PR (e.g. `31151, #31152, PLAT-333`); the script normalizes whitespace, prepends `#` to bare digits, and joins with `, ` so the title renders as `[#31151, #31152, PLAT-333] <Component>: <Title>`.
- **`-t`**: title body **without** the `[<issue>] ` prefix but **with** the `Component: ` prefix (e.g. `DocDB: Fix flake`).
- **`-d` (required)**: path to a markdown file with the PR description (the "what / why" prose derived from branch commits). The script makes this the `## Summary` section.
- **`-U` (optional, sometimes required)**: path to a markdown file with upgrade/rollback notes. The script makes this the `## Upgrade/Rollback safety` section, inserted **between Summary and Test plan**. **Pass this whenever the branch makes an upgrade-relevant decision.** The script enforces it **mechanically when any `.proto` file changes** (`exit 1` if `-U` is missing and a `*.proto` diff exists) — wire-format changes have to spell out forward and backward behavior on a mixed-version cluster and what rollback looks like. **Also include for** (script can't detect, but the src/AGENTS.md rule expects it): gflag default flips that change observable behavior, catalog schema bumps, on-disk-format changes, RPC-versioning tweaks, migration scripts. When unsure, pass `-U` with a brief note rather than skipping.
- **`-T` (required)**: path to a markdown file with the test plan (typically a checkbox list). **Always supply this** — the script errors out if `-T` is missing or the file is empty. If the change is trivial enough that you think no test plan applies, write an explicit one-line checkbox saying so (e.g., `No runtime behavior change; visually inspected diff`) and confirm with the user before proceeding.
- **`-r`**: comma-separated handles and/or team slugs (`alice,bob,yugabyte/db-approvers`). Optional.
- **`-D` (optional)**: open the PR as a GitHub draft (`gh pr create --draft`). Use when the user wants to read the rendered PR before notifications fire and before reviewers are auto-pinged. After the user reviews, convert with `gh pr ready <num>`. **Use whenever the user asks for "draft PR(s)" or says they want to inspect the PR on GitHub before sending it out.** When `-D` is set, the script still requests reviewers via the REST endpoint as usual — GitHub silences review-requested notifications until the PR is marked ready, so the two settings combine cleanly.

Exit codes:
- `0` — PR created. Last stdout line is the PR URL.
- `2` — rebase conflict; resolve, `git rebase --continue`, then re-run.
- `3` — lint failed; fix as a NEW commit (do not amend a pushed commit, per `src/AGENTS.md`), then re-run.
- `1` — pre-flight failure (dirty tree, missing remote, etc.).

Confirm the title and body with the user before invoking the script.

### Step 5b (optional): Auto-recover from trivial rebase conflicts and lint errors

If Step 5 exits `2` (rebase conflict) or `3` (lint), inspect the failure and try once to fix automatically before going back to the user:

- **Rebase conflicts** — only auto-resolve **trivial** conflicts (whitespace-only differences, adjacent-but-non-overlapping hunks, conflicts where both sides are byte-identical after whitespace normalization). For each such file, accept the resolution that preserves the branch's intent, `git add` it, and `git rebase --continue`. Anything involving renamed identifiers, signature changes, or refactors must be escalated to the user — do not guess.
- **Lint errors** — if the linter reports auto-fixable issues (e.g. trailing whitespace, missing newlines), apply the fix as a **new commit** (`Fix lint`), not an amend. If the errors require judgment (logic changes, unused-variable removal that might be load-bearing), escalate.

After the auto-fix, re-run Step 5 once. If it fails again, stop and surface the conflict/lint output to the user.

### Step 6: Report back to the user

Output:

- The PR URL (printed by `gh pr create`)
- The constructed title
- The base branch the PR targets

Then clean up any temp files created during this run (e.g., `/tmp/claude/commit-msg-<issue>.txt`, `/tmp/claude/pr-body-<issue>.md`).

## Notes

- **Never push to `yugabyte/yugabyte-db`, or use `gh pr create`.** Always use the create-pr.sh script.
- The title format is strict: `[<issue>] <Component>: <Title>`. Don't deviate.
- Never force-push without explicit user permission; when authorized, prefer `--force-with-lease`.
- CI runs automatically on GitHub PRs, so there is no `trigger jenkins` step (unlike the Phorge `create-review` skill).
- `gh pr create --repo yugabyte/yugabyte-db` opens the PR in the upstream repo even when the branch lives on a fork — the `head:` field is inferred from the tracking branch.
- **`gh pr edit` is broken on this repo** — it errors with `GraphQL: Projects (classic) is being deprecated... (repository.pullRequest.projectCards)`. This affects `--body-file`, `--add-reviewer`, `--add-label`, and other post-creation edit flags. For any post-creation update to PR body / reviewers / labels, use the REST API directly:
  - **Body update:** `jq -Rs '{body: .}' < new-body.md | gh api -X PATCH /repos/yugabyte/yugabyte-db/pulls/<num> --input -`
  - **Add reviewers:** `gh api -X POST /repos/yugabyte/yugabyte-db/pulls/<num>/requested_reviewers -f 'reviewers[]=user1' -f 'reviewers[]=user2'` (and `-f 'team_reviewers[]=team-slug'` for org/team reviewers — strip the `org/` prefix; team_reviewers takes the slug only).
