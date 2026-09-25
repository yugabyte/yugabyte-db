---
title: Detect drift
headcontent: yb-voyager schema detect-drift
linkTitle: detect drift
description: YugabyteDB Voyager schema detect-drift reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-detect-drift
    parent: schema-migration
    weight: 30
tags:
  feature: tech-preview
type: docs
---

Reports how the PostgreSQL source schema changed while a migration was running, and what to do about each change.

yb-voyager records a snapshot of the source schema during [export schema](../export-schema/), when [export data](../../data-migration/export-data/) starts, periodically while export data runs, and when export data exits. The `schema detect-drift` command compares consecutive snapshots, and compares the last one with a live read of the source. It writes the result as a report. The command is _read-only_: it never changes migration state and never applies anything on the target.

The command reads schema snapshots that export schema and export data record by default. If the export commands ran with `--disable-schema-snapshot-capture true`, or on an earlier yb-voyager version that didn't record snapshots, there is nothing to compare. The command then fails instead of reporting that "no drift" was found.

## Syntax

```text
Usage: yb-voyager schema detect-drift [ <arguments> ... ]
```

### Arguments

The valid *arguments* for schema detect-drift are described in the following table. The configuration file section is `schema-detect-drift`.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}
| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |

| -e, --export-dir |

```yaml {.nocopy}
export-dir:
```

| Path to the export directory of the migration. Required. The directory must already contain a migration; the command never creates one. |

| --source-db-user |

```yaml {.nocopy}
source:
  db-user:
```

| Source database user. Required. |

| --source-db-password |

```yaml {.nocopy}
source:
  db-password:
```

| Source database password. You can also set the password with the `SOURCE_DB_PASSWORD` environment variable. If you don't provide a password, yb-voyager prompts you at runtime. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |

| --source-db-name |

```yaml {.nocopy}
source:
  db-name:
```

| Source database name. Required. |

| --source-db-schema |

```yaml {.nocopy}
source:
  db-schema:
```

| Comma-separated list of schemas to compare. Required. Accepts the same values as [export schema](../export-schema/#arguments). |

| --source-db-host |

```yaml {.nocopy}
source:
  db-host:
```

| Domain name or IP address of the machine on which the source database server is running. Same as the [export commands](../export-schema/#arguments). |

| --source-db-port |

```yaml {.nocopy}
source:
  db-port:
```

| Port number of the source database server. Same as the export commands. |

| --source-db-type |

```yaml {.nocopy}
source:
  db-type:
```

| Source database type. Defaults to `postgresql`. PostgreSQL is the only supported source. |

| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
source:
  ssl-mode:
```

| SSL mode for the source database. Same as the export commands. One of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |

| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
source:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |

| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
source:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair. |

| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
source:
  ssl-crl:
```

| Path to a file containing the SSL certificate revocation list (CRL). |

| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
source:
  ssl-root-cert:
```

| Path to a file containing SSL certificate authority (CA) certificate(s). |

| --output-format |

```yaml {.nocopy}
schema-detect-drift:
  output-format:
```

| Format of the report: `html` or `json`. If not set, both are written. |

| --table-list |

```yaml {.nocopy}
schema-detect-drift:
  table-list:
```

| Comma-separated list of tables to compare. Glob patterns are allowed, and names resolve the same way as [`--table-list`](../../data-migration/export-data/#arguments) in export data. A partitioned table includes all of its partitions. An entry without a schema name is looked up in `public`, so if `--source-db-schema` doesn't include `public`, write every entry as `schema.table`. Can't be combined with `--exclude-table-list`. |

| --exclude-table-list |

```yaml {.nocopy}
schema-detect-drift:
  exclude-table-list:
```

| Comma-separated list of tables to leave out. Glob patterns are allowed. Can't be combined with `--table-list`. |

| --object-type-list |

```yaml {.nocopy}
schema-detect-drift:
  object-type-list:
```

| Object types to compare: `TABLE`, `COLUMN`. Default: both. Can't be combined with `--exclude-object-type-list`. |

| --exclude-object-type-list |

```yaml {.nocopy}
schema-detect-drift:
  exclude-object-type-list:
```

| Object types to leave out: `TABLE`, `COLUMN`. Can't be combined with `--object-type-list`. |

| -l, --log-level |

```yaml {.nocopy}
schema-detect-drift:
  log-level:
```

| Log level for this command. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| --send-diagnostics |

```yaml {.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -h, --help | — | Command line help. |

| -c, --config-file | — | Path to a [configuration file](../../configuration-file/). |

{{</table>}}

## Output

Reports are written to `<export-dir>/reports/drift_analysis_report.html` and `<export-dir>/reports/drift_analysis_report.json`. Each run overwrites the previous report.

The console shows a summary of the comparison window, stored captures, intervals compared, schemas compared, tables compared, and the number of changes found.

## Reading the report

The HTML report shows the migration as a timeline, from export schema to the live read of the source taken when the command runs. Each change is listed in the interval in which it happened, labeled with what the migration was doing at the time, such as "export data: running".

Every change has a severity (such as, Breaks the migration: unrecoverable, Breaks the migration: recoverable, Potential impact, or Advisory), a description of its impact on the migration, and the corrective step. The JSON report carries the same information for scripts.

## Example

Configuration file:

```sh
yb-voyager schema detect-drift --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager schema detect-drift --export-dir /dir/export-dir \
        --source-db-host 127.0.0.1 \
        --source-db-user ybvoyager \
        --source-db-name sales \
        --source-db-schema public,inventory \
        --output-format html
```

## Limitations

- Applicable to PostgreSQL sources only.
- Only tables and columns are compared. Indexes, constraints, views, functions, and other objects are not.
- Only source-side changes are reported, and only up to cutover to the target.
- The report says what to do; it doesn't generate or apply DDL.
- With [iterative cutover](../../iterative-cutover/), each iteration records its snapshots in its own export directory, and the command reports only on the directory passed in `--export-dir`. The main export directory covers the first iteration. To report on a later iteration, pass that iteration's export directory: `<export-dir>/live-data-migration-iterations/live-data-migration-iteration-<N>/export-dir`. There is no single report across iterations.
- If a table is renamed during the migration, filtering with `--table-list` on either name doesn't show its full history. An unfiltered run shows both.
