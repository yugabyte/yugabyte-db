# yba CLI query command reference

Read-only commands (`list` / `describe` / `get`) by resource, with the
notable flags. Every command also inherits `-H/--host`, `-a/--apiToken`,
`-o/--output` (table|json|pretty), `-l/--logLevel`. Full generated docs for
any command live at `managed/yba-cli/docs/yba_<path>.md` — check there if a
flag isn't listed here.

## universe

| Command | Key flags |
|---|---|
| `yba universe list` | `-n/--name` (optional filter) |
| `yba universe describe` | `-n/--name` (required); `-o cli-flag\|cli-json\|cli-yaml` renders output usable as `yba universe create` input |
| `yba universe node list` | target universe via parent flag: `yba universe node list -n <universe-name>` |
| `yba universe table list` | parent flag `-n <universe-name>`; then `--table-name`, `--include-parent-table-info`, `--include-colocated-parent-tables`, `--xcluster-supported-only` |
| `yba universe table describe` | parent `-n <universe-name>`, plus table identifier flags |
| `yba universe table namespace list` | parent `-n <universe-name>` |
| `yba universe table tablespace list` / `describe` | parent `-n <universe-name>` |
| `yba universe support-bundle list` / `describe` | parent flag `-n <universe-name>` |
| `yba universe upgrade gflags get` | parent universe scoping flags |

## task

| Command | Key flags |
|---|---|
| `yba task list` | `-u/--uuid`, `--universe-name` (both optional filters) |

## backup

| Command | Key flags |
|---|---|
| `yba backup list` | `--universe-uuids`, `--universe-names` (comma-separated) |
| `yba backup describe` | `-u/--uuid` (required) |
| `yba backup schedule list` | `--universe-name` (required) |
| `yba backup schedule describe` | describe by uuid/name — see doc |
| `yba backup restore list` | `--universe-uuids`, `--universe-names` |
| `yba backup restore describe` | uuid of the restore |
| `yba backup pitr list` | parent flag `--universe-name` (required) |
| `yba backup pitr describe` | parent flag `--universe-name` (required) |

## provider (+ cloud subtypes: aws, gcp, azu/azure, onprem, kubernetes)

| Command | Key flags |
|---|---|
| `yba provider list` | `-n/--name`, `-c/--code` (aws\|gcp\|azu\|onprem\|kubernetes) |
| `yba provider describe` | `-n/--name` (required), `-c/--code` |
| `yba provider aws list` / `describe` | scoped to AWS providers, no extra filters beyond parent |
| `yba provider <cloud> instance-type list` / `describe` | list/describe instance types registered for that provider |
| `yba provider onprem node list` | list on-prem nodes for a provider |

## alert (preview command group — needs `YBA_FF_PREVIEW=true`, see Notes below)

| Command | Key flags |
|---|---|
| `yba alert list` | `--configuration-uuid`, `--configuration-types`, `--severities`, `--source-uuids`, `--source-name`, `--states`, `--uuids`, `--sorting-field`, `--direction` |
| `yba alert describe` | `-u/--uuid` (required) |
| `yba alert channel list` | `-n/--name`, `--type` (email\|webhook\|pagerduty\|slack) |
| `yba alert channel describe` (and `email`/`webhook`/`pagerduty`/`slack` subtypes) | by name/uuid |
| `yba alert destination list` / `describe` | `-n/--name` |
| `yba alert maintenance-window list` | `-n/--name`, `--states`, `--uuids` |
| `yba alert policy list` | `--active`, `--destination-type`, `--destination`, `--name`, `--severity`, `--target-uuids`, `--target-type`, `--template`, `--uuids`, `--sorting-field`, `--direction` |
| `yba alert policy template list` | lists available alert templates |

## storage-config (+ subtypes: s3, gcs, nfs, azure)

| Command | Key flags |
|---|---|
| `yba storage-config list` | `-n/--name`, `-c/--code` (s3\|gcs\|nfs\|az) |
| `yba storage-config describe` | `-n/--name` (required) |
| `yba storage-config <s3\|gcs\|nfs\|azure> list` / `describe` | scoped to that backend |

## ear (Encryption at Rest) — subtypes: aws, azure, gcp, hashicorp-vault, ciphertrust

| Command | Key flags |
|---|---|
| `yba ear list` / `describe` | list/describe EAR configs |
| `yba ear <aws\|azure\|gcp\|hashicorp-vault\|ciphertrust> list` / `describe` | scoped to that KMS backend |

## eit (Encryption in Transit) — subtypes: custom-ca, hashicorp-vault, k8s-cert-manager, self-signed

| Command | Key flags |
|---|---|
| `yba eit list` / `describe` | list/describe EIT configs |
| `yba eit <custom-ca\|hashicorp-vault\|k8s-cert-manager\|self-signed> list` / `describe` | scoped to that cert backend |

## telemetry-provider (preview command group — needs `YBA_FF_PREVIEW=true`) — subtypes: datadog, splunk, loki, awscloudwatch, gcpcloudmonitoring

| Command | Key flags |
|---|---|
| `yba telemetry-provider list` / `describe` | list/describe telemetry provider configs |
| `yba telemetry-provider <subtype> list` / `describe` | scoped to that backend |

## rbac

| Command | Key flags |
|---|---|
| `yba rbac role list` | `-n/--name`, `--type` (system\|custom) |
| `yba rbac role describe` | by name/uuid |
| `yba rbac role-binding list` | `-e/--email` |
| `yba rbac permission list` | `-n/--name`, `--resource-type` (universe\|role\|user\|other) |

## runtime-config

| Command | Key flags |
|---|---|
| `yba runtime-config scope list` | `--type` (universe\|customer\|provider\|global) |
| `yba runtime-config scope describe` | by scope uuid |
| `yba runtime-config scope key get` | `-u/--uuid` (scope), `-n/--name` (key), both required |
| `yba runtime-config key-info list` | `--type` (universe\|customer\|provider\|global) |
| `yba runtime-config key-info describe` | by key name |

## user / group / customer

| Command | Key flags |
|---|---|
| `yba user list` | `-e/--email` |
| `yba user describe` | `-e/--email` (required) |
| `yba group list` | `-n/--name`, `-c/--auth-code` |
| `yba customer list` / `describe` | no extra filters (single customer per YBA typically) |

## xcluster

| Command | Key flags |
|---|---|
| `yba xcluster list` | `--source-universe-name`, `--target-universe-name` (both required) |
| `yba xcluster describe` | `-u/--uuid` (required) |

## yb-db-version

| Command | Key flags |
|---|---|
| `yba yb-db-version list` | `--deployment-type` (x86_64\|aarch64\|kubernetes), `--type` (lts\|sts\|preview) |
| `yba yb-db-version describe` | by version string |

## ha (High Availability)

| Command | Key flags |
|---|---|
| `yba ha describe` | describes current HA config |
| `yba ha replication describe` | describes HA replication status |

## auth / oidc / ldap (config inspection only — do not use `login`/`register`/`auth` here)

| Command | Key flags |
|---|---|
| `yba oidc describe` | describes OIDC config (preview — needs `YBA_FF_PREVIEW=true`) |
| `yba ldap describe` | describes LDAP config |

## Misc

- `yba tree` — prints the full command tree; useful to discover a command
  this reference doesn't cover.
- `yba <resource> <subcommand> --help` — authoritative, always up to date;
  prefer it over this file when they disagree.

## Preview commands

`alert`, `oidc`, and `telemetry-provider` are hidden behind a feature flag —
they don't show up in `yba --help` or exist at all unless you set
`YBA_FF_PREVIEW=true` in the environment before invoking `yba`, e.g.:

```bash
YBA_FF_PREVIEW=true yba alert list --states active
```
