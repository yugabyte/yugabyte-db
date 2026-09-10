---
name: yba-query-cli
description: >-
  Query a running YugabyteDB Anywhere (YBA) control plane using the `yba` CLI
  (managed/yba-cli) — list/describe universes, tasks, backups, providers,
  alerts, users, storage/EAR/EIT configs, xClusters, RBAC, runtime config,
  telemetry providers, and more. Use when the user wants to inspect, list,
  describe, or look up the current state of a live YBA instance. Read-only:
  do not use for creating, editing, deleting, pausing/resuming, or upgrading
  YBA resources.
---

# Query YBA with the yba CLI

This skill is for **read-only lookups** against a live YugabyteDB Anywhere
instance via the `yba` CLI, whose source lives at `managed/yba-cli` in this
repo. It answers questions like "what universes exist on this YBA?", "is
there a task running on universe X?", "what's this provider's config?" — not
for changing anything.

**Guardrail:** only ever run `list`, `describe`, or `get` subcommands (and the
read-only `yba tree` / `--help`). Never run `create`, `edit`, `delete`,
`pause`, `resume`, `restart`, `upgrade`, `abort`, `retry`, or similar mutating
verbs under this skill — those are a separate, explicit ask that the user
must request directly, with its own confirmation.

## Step 1: Get the `yba` binary

Check first: `which yba` or `managed/yba-cli/yba --help`. If it's missing,
build it from source (no need for the full release pipeline — a plain `go
build` is enough for querying):

```bash
cd managed/yba-cli && go build -o yba .
```

Then invoke it as `managed/yba-cli/yba <command>` (or add that path to `PATH`
for the session).

## Step 2: Point it at the right host + credentials

The CLI needs a **host** (`-H`/`--host`, default `http://localhost:9000`) and
an **API token** (`-a`/`--apiToken`). Resolution order: `-H`/`-a` flags >
`YBA_HOST`/`YBA_APITOKEN` env vars > the config file at
`$HOME/.yba-cli/.yba-cli.yaml` (written by a prior `yba auth`).

- If the user already has a working config (`~/.yba-cli/.yba-cli.yaml`
  exists, or `yba universe list` succeeds with no extra flags), just use it —
  don't re-auth.
- Otherwise, ask the user for the host and an API token (generated in the YBA
  UI under Profile → API Keys). **Prefer passing them per-invocation via
  `-H`/`-a` flags or exporting `YBA_HOST`/`YBA_APITOKEN` env vars for this
  session**, rather than running `yba auth`, since `yba auth` persists into
  `~/.yba-cli/.yba-cli.yaml` and could silently overwrite a config the user
  already has for a different YBA instance. Only run `yba auth -f -H <host>
  -a <token>` if the user explicitly wants the CLI configured persistently.
- Don't use `yba login -p <password>` for this skill — it takes the password
  as a plain CLI argument (leaks into shell history / process listing) and
  also persists to the config file. An API token is the right credential for
  read-only querying.

## Step 3: Run the query

Use `-o json` when you need to parse or filter the output (e.g. pipe to
`jq`); the default `table` format is for human eyeballing. `pretty` gives
indented JSON.

For the full command tree and flags, see [reference.md](reference.md) in
this skill directory, or run `yba tree` / `yba <resource> <subcommand>
--help` / read `managed/yba-cli/docs/yba_<resource>.md` directly.

Quick examples:

```bash
yba universe list -o json
yba universe describe --name <universe-name> -o json
yba task list --universe-name <universe-name>
yba backup list --universe-names <universe-name> -o json
yba alert list --states active
yba provider list -o json
```

## Notes

- Command and flag names are case-insensitive, but stick to the documented
  casing shown in `--help` / reference.md.
- Filters that take lists (`--uuids`, `--states`, `--universe-names`, etc.)
  are comma-separated strings, not repeated flags.
- Some subcommands take their target as a flag on the **parent** command
  rather than on `list` itself — e.g. `yba universe node list -n
  <universe-name>`, `yba universe table list -n <universe-name>`, `yba
  backup pitr list --universe-name <universe-name>`.
- `--wait`/`--timeout` only matter for commands that kick off a YBA task;
  they're irrelevant to pure reads and safe to ignore.
- `alert`, `oidc`, and `telemetry-provider` are preview command groups —
  they don't exist unless `YBA_FF_PREVIEW=true` is set in the environment,
  e.g. `YBA_FF_PREVIEW=true yba alert list`.
