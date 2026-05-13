---
name: create-review
description: >-
  Create a YugabyteDB Phorge review (diff) for the current branch's changes.
  Use when the user wants to publish their changes for review.
---

# Create Review

Create a Phorge review for the current branch using `arc diff --create`.

## Confidentiality — read before you write anything

**Treat Phorge as public.** Two reasons: (1) Phorge revisions and the Jenkins logs they trigger are visible to anyone with company access, not just the reviewers you @-mention; (2) anything you put into the test code, code comments, fixtures, or golden outputs on this diff lands on the public GitHub repo verbatim when this work is later upstreamed via a PR — there is no separate scrubbing pass at that boundary. So apply the public-repo rules now: diff title, summary, test plan, inline comments, commit messages, code, tests, fixtures, golden output, and filenames all get the same treatment as a public GitHub PR.

**Never put any of the following into the diff title, summary, test plan, Phorge comments, commit messages, code comments, test code, test fixtures, golden output, or filenames:**

- **Customer-identifying data** — company names, account IDs, universe/cluster names or UUIDs, support case numbers, environment names, region/zone names tied to a customer deployment. Substitute with `customer-1`, `acme-corp`, or a generic description.
- **PII** — real names, real emails, real phone numbers, real postal addresses, real IP addresses (public or private). In tests use only the documentation ranges: IPv4 `192.0.2.0/24` / `198.51.100.0/24` / `203.0.113.0/24` (RFC 5737), IPv6 `2001:db8::/32` (RFC 3849), hostnames `example.com` / `example.org` / `example.net` (RFC 2606), names `Alice`/`Bob`/`Carol`, phone numbers `555-0100`–`555-0199`.
- **Unanonymized customer schema or queries** — table names, column names, SQL text, query plans, or sample rows pulled from a real customer report. Reconstruct a synthetic minimal reproducer with generic identifiers (`t1`, `users`, `id`, `value`) that demonstrates the same defect.
- **Secrets / credentials** — API keys, tokens, passwords, TLS certificates, private keys, license keys, kubeconfigs, production S3 buckets, internal Slack/JIRA/Linear URLs (other than referencing them by ticket key like `PLAT-20518`), internal hostnames, vault paths. Use placeholders like `AKIAIOSFODNN7EXAMPLE` in mocks.
- **YugabyteDB-internal information not yet public** — unreleased roadmap, internal SLAs, embargoed security findings, internal infra hostnames.

**This applies to the test code and test data you write too** — `.cc`, `.py`, `.java`, `.sql`, golden `.out` files, YAML fixtures, mock responses. If a customer's reproducer uses `acme_orders` with a `customer_email` column populated with real addresses, rewrite it as `t1`/`email` with `user@example.com` before the test goes anywhere near a diff.

**When the source is a customer report:** read the original from the internal source (JIRA / support ticket / Slack) — don't paste or link it. Reproduce locally with synthetic inputs. Land only the synthetic reproducer. Reference the issue by internal ticket ID (`PLAT-20518`) only.

**If you're unsure whether a string is sensitive, don't write it down — ask the user.** See `src/AGENTS.md` § Confidentiality for the canonical list.

This rule applies at every step below — Step 1 (commit message), Step 3 (issue body if auto-creating), Step 5 (title), Step 6 (summary / test plan), Step 7 (comment text).

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

> **Confidentiality — final scrub before publishing.** Phorge is treated as public (company-wide visible + test code lands on the public GitHub repo verbatim when upstreamed). Before composing the message file, re-read the summary, test plan, commit messages, **and the test code being added**, and confirm none of the following appear: customer names or identifiers (universe UUIDs, account IDs, support cases, environment names, region/zone names); PII (real names / emails / phone numbers / postal addresses / IP addresses — use RFC 5737/3849 documentation ranges in tests); unanonymized customer schemas (table / column / query text / query plans / sample rows from a real customer — reconstruct a synthetic reproducer); credentials, tokens, certificates, private keys, or license keys; internal-only hostnames, URLs, Grafana/Slack/Linear links, or vault paths; unreleased internal information (roadmap, SLAs, embargoed security findings, internal infra hostnames). See the top of this skill and `src/AGENTS.md` § Confidentiality for the full rule. If unsure whether a string is sensitive, don't write it down — ask the user.

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
