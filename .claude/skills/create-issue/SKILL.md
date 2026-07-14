---
name: create-issue
description: >-
  Create a GitHub issue (or JIRA ticket) tracking a YugabyteDB change or bug.
  Use when the user needs a tracker reference and doesn't have one yet — e.g.
  before opening a PR/diff, or to file a standalone bug/feature request.
allowed-tools: Bash(gh issue create *), Bash(gh issue view *), Bash(gh auth status), Read
model: sonnet
---

# Create Issue

Create a tracking issue for a YugabyteDB change: either a **GitHub issue** in
`yugabyte/yugabyte-db` (the default for core DB code) or a **JIRA ticket** (the
default for YBA/platform work). Return the resulting issue number / JIRA key so
it can feed into `/create-pr` or `/create-diff`.

## Confidentiality — read before you write anything public

**The upstream repo and every GitHub issue (title/body/labels) are PUBLIC on the
internet.** Anything you (the agent) write into an issue is permanent and indexed
by search engines. A leak cannot be unsent. JIRA is internal, but the same
hygiene applies — don't paste customer data you don't need.

**Never put any of the following into the issue title, body, or labels:**

- **Customer-identifying data** — company names, account IDs, universe/cluster
  names or UUIDs, support case numbers, environment names, region/zone names tied
  to a customer deployment. Substitute with `customer-1`, `acme-corp`, or a
  generic description.
- **PII** — real names, real email addresses, real phone numbers, real postal
  addresses, real IP addresses (public or private). Use the RFC 5737/3849
  documentation ranges and RFC 2606 hostnames (`example.com`) if you need
  examples.
- **Unanonymized customer schema or queries** — table names, column names, SQL
  text, query plans, or sample rows pulled from a real customer report.
  Reconstruct a synthetic minimal reproducer using generic identifiers (`t1`,
  `users`, `id`, `value`).
- **Secrets / credentials** — API keys, tokens, passwords, TLS certificates,
  private keys, license keys, kubeconfigs, production buckets, internal
  Slack/JIRA/Linear URLs, internal hostnames, vault paths.
- **YugabyteDB-internal information not yet public** — unreleased roadmap,
  internal SLAs, embargoed security findings, internal infra hostnames.

**When the source is a customer report:** read the original from the internal
source (JIRA / support ticket / Slack) — never paste or link it from a public
artifact. Reference the internal ticket ID (`PLAT-20518`) only; don't quote
customer-facing text into a public GitHub issue.

**If you're unsure whether a string is sensitive, don't write it down — ask the
user.** See `src/AGENTS.md` § Confidentiality for the canonical list. Every GitHub
issue template also carries a mandatory checkbox confirming the issue contains no
sensitive information — you are affirming that on the user's behalf, so scrub
first.

## Prerequisites

For GitHub issues, this skill requires the `gh` CLI installed and authenticated:

1. **Install `gh`**: see https://cli.github.com/.
2. **Authenticate**: `gh auth login` (or `gh auth status` to verify).

For JIRA tickets, the Atlassian MCP tool `createJiraIssue` must be available
(load it via ToolSearch: `select:mcp__claude_ai_Atlassian__createJiraIssue`).

## Workflow

### Step 1: Decide the tracker — GitHub vs JIRA

Pick the default from what the change touches; ask only if it's genuinely
ambiguous:

- Core DB code (`src/`, `java/`, `cmake_modules/`, `build-support/`, `.github/`,
  `.claude/`) → **GitHub issue** in `yugabyte/yugabyte-db`.
- YBA / platform code (`managed/`) → **JIRA ticket**.

If the user has already stated a preference or named a project, honor it.

### Step 2: Gather title metadata

1. **Component** — infer from the files changed or the conversation
   (`git diff --name-only` if there's a branch/diff in flight):
   - `src/yb/` → `DocDB`
   - `src/postgres/` → `YSQL`
   - `src/yb/yql/cql/` → `YCQL`
   - `managed/` → `YBA`
   - `yugabyted-ui/` → `yugabyted`
   - `docs/` → `Docs`
   - `.claude/`, `AGENTS.md`, `CLAUDE.md` → `ClaudeCode`
   - `build-support/`, `cmake_modules/`, `.github/` → `Build`
   - mixed/cross-cutting → ask the user

2. **Title** — a short, specific description of the bug or change. Propose one
   from the conversation / commit messages if available. Confirm with the user.

3. **Description** — the "what / why": what the issue is, repro steps for a bug,
   or the motivation for a feature. Draft it and confirm with the user.

### Step 3a: Create a GitHub issue

1. **Pick a matching issue template** from `.github/ISSUE_TEMPLATE/` based on the
   component:
   - `docDB.yml` — DocDB / core storage
   - `ysql.yml` — YSQL
   - `ycql.yml` — YCQL
   - `cdc.yml` — CDC
   - `ui.yml` — YBA / UI
   - `yugabyted.yml` — yugabyted
   - `docs.yml` — docs
   - `feature_request.yml` — generic fallback for tooling / other, or any feature
     request

   **Never file a GitHub issue without a template.** Every issue must be built
   from one of the `.github/ISSUE_TEMPLATE/` templates — do not hand-craft a bare
   `gh issue create` body. If no component-specific template fits, fall back to
   `feature_request.yml`.

2. **Read the chosen template YAML.** Then:
   - (a) Collect its `labels:` list and pass them via `--label`.
   - (b) Construct a markdown body **mirroring the template's `body` sections**.
     For each field:
     - A `textarea` labeled *Description* → a `## Description` heading followed by
       the user-facing content.
     - An `Issue Type` `dropdown` → pick one of the listed `options` (e.g.
       `kind/bug`, `kind/enhancement`, `kind/failing-test`, `kind/new-feature`)
       that matches the change, and state it in the body.
     - The sensitivity `checkboxes` → include a line confirming the issue contains
       no sensitive information (you've scrubbed per the Confidentiality section).

3. **Confirm the title and body with the user before creating.**

4. Write the body to a temp file and run:

   ```
   gh issue create \
     --repo yugabyte/yugabyte-db \
     --assignee @me \
     --title "<title>" \
     --body-file /tmp/claude/issue-body.md \
     --label "<comma-separated labels from template>"
   ```

   **Always assign the issue to the current user** (`--assignee @me`).

5. Capture the issue number from the URL `gh issue create` prints.

### Step 3b: Create a JIRA ticket

1. Ask the user which project (e.g., `PLAT`) if not already known.
2. Load the Atlassian tool via ToolSearch, then call
   `mcp__claude_ai_Atlassian__createJiraIssue` with the confirmed summary and
   description. **Confirm the summary/description with the user before creating.**
3. Capture the resulting JIRA key.

### Step 4: Report back

Output:

- The issue URL (from `gh issue create`) or the JIRA key + link.
- The reference in the form you'd hand to `/create-pr` or `/create-diff`
  (a GitHub number like `#31151`, or a JIRA key like `PLAT-20518`).

Then clean up any temp files created during this run.

## Notes

- Don't reuse an issue across unrelated in-flight work — file a fresh one when
  the change is unrelated, even if a related closed/merged PR used a similar
  issue.
- The GitHub title the template proposes (e.g. `[DocDB] Title`) is the raw issue
  title. The `[<issue>] <Component>: <Title>` PR/commit format is applied later by
  `/create-pr` or `/create-diff` — don't pre-format the issue title that way.
- This skill only *creates* the tracker. Opening the PR/diff is a separate step.
