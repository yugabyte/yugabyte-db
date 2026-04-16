---
title: assess migration reference
headcontent: yb-voyager assess-migration
linkTitle: Assess migration
description: YugabyteDB Voyager assess migration reference
menu:
  stable_yugabyte-voyager:
    identifier: assess-migration-voyager
    parent: yb-voyager-cli
    weight: 1
tags:
  feature: tech-preview
type: docs
---

The following page describes the following assess commands:

- [assess migration](#assess-migration)
- [assess migration bulk](#assess-migration-bulk)

## assess migration

[Assess the migration](../../migrate/assess-migration) from source (PostgreSQL, Oracle) database to YugabyteDB.

### Syntax

```text
Usage: yb-voyager assess-migration [ <arguments> ... ]
```

### Arguments

The valid *arguments* for assess migration are described in the following table:

{{<table>}}
| <div style="width:150px">CLI flag</div> | Config file parameter | Description |

| :--- | :-------- | :---------- |

| --run-guardrails-checks |

```yaml{.nocopy}
assess-migration:
  run-guardrails-checks:
```

| Run guardrails checks during migration. <br>Default: true<br>Accepted values: true, false, yes, no, 0, 1 |

| --assessment-metadata-dir |

```yaml{.nocopy}
assess-migration:
  assessment-metadata-dir:
```

| Directory path where assessment metadata like source database metadata and statistics are stored. Optional flag, if not provided, it will be assumed to be present at default path inside the export directory. |

| --iops-capture-interval |

```yaml{.nocopy}
assess-migration:
  iops-capture-interval:
```

| Interval (in seconds) at which Voyager will gather IOPS metadata from the source database for the given schema(s). <br> Default: 120 |

| --target-db-version |

```yaml{.nocopy}
assess-migration:
  target-db-version:
```

| Specifies the target version of YugabyteDB in the format `A.B.C.D`.<br>Default: latest stable version |

| --oracle-db-sid |

```yaml{.nocopy}
source:
  oracle-db-sid:
```

| Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only. |
| --oracle-home |

```yaml{.nocopy}
source:
  oracle-home:
```

| Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only. |
| [--oracle-tns-alias](../yb-voyager-cli/#oracle-options) |

```yaml{.nocopy}
source:
  oracle-tns-alias:
```

| TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --source-db-host |

```yaml{.nocopy}
source:
  db-host:
```

| Domain name or IP address of the machine on which the source database server is running. <br>Default: localhost |
| --source-db-name |

```yaml{.nocopy}
source:
  db-name:
```

| Source database name. |
| --source-db-password |

```yaml{.nocopy}
source:
  db-password:
```

| Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port |

```yaml{.nocopy}
source:
  db-port:
```

| Source database server port number. <br>Default: PostgreSQL (5432), Oracle (1521) |
| --source-db-schema |

```yaml{.nocopy}
source:
  db-schema:
```

| Source schema name(s) to export. In case of PostgreSQL, it can be single or a comma-separated list of schemas: `schema1,schema2,schema3`. |
| --source-db-type |

```yaml{.nocopy}
source:
  db-type:
```

| Source database type (postgresql, oracle). |
| --source-db-user |

```yaml{.nocopy}
source:
  db-user:
```

|  Username of the source database. |
| [--source-ssl-cert](../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-crl:
```

| Path of the file containing source SSL Root Certificate Revocation List (CRL). |
| [--source-ssl-key](../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair.|
| [--source-ssl-mode](../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-mode:
```

| One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-root-cert:
```

| Path of the file containing source SSL Root Certificate. |
| --primary-only |

```yaml{.nocopy}
source:
      primary-only:
```

| Skip read replica discovery and assessment, and assess only the primary database. PostgreSQL migrations only.<br>Default: false |
| --source-read-replica-endpoints |

```yaml{.nocopy}
source:
      read-replica-endpoints:
```

| Comma-separated list of read replica endpoints. PostgreSQL migrations only.<br>
Format endpoints as `host:port`.<br>
For example, `host1:5432, host2:5433`.<br>
Default port: 5432 |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| --start-clean | — | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |

| -h, --help | — |Command line help. |
| -y, --yes | — |Answer yes to all prompts during the export schema operation. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -c, --config-file | — | Path to a [configuration file](../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager assess-migration --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager assess-migration --source-db-type postgresql \
        --source-db-host hostname --source-db-user username \
        --source-db-password password --source-db-name dbname \
        --source-db-schema schema1,schema2 --export-dir /path/to/export/dir
```

## assess migration bulk

[Assess the migration](../../migrate/assess-migration) from multiple source (Oracle) database schemas to YugabyteDB.

### Syntax

```text
Usage: yb-voyager assess-migration-bulk [ <arguments> ... ]
```

### Arguments

The valid *arguments* for assess migration bulk are described in the following table:

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| --bulk-assessment-dir | Directory path where assessment data is output. |
| --continue-on-error | Print the error message to the console and continue to next schema assessment.<br>Default: true.<br>Accepted parameters: true, false, yes, no, 0, 1. |
| --fleet-config-file | Path to the CSV file with connection parameters for schema(s) to be assessed. The first line must be a header row. <br>Fields (case-insensitive): 'source-db-type', 'source-db-host', 'source-db-port', 'source-db-name', 'oracle-db-sid', 'oracle-tns-alias', 'source-db-user', 'source-db-password', 'source-db-schema'.<br>Mandatory: 'source-db-type', 'source-db-user', 'source-db-schema', and one of ['source-db-name', 'oracle-db-sid', 'oracle-tns-alias']. |
| -h, --help | Command line help. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |
| -y, --yes | Assume answer to all prompts during migration. <br>Default: false |
| -l, --log-level | Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

### Example

```sh
yb-voyager assess-migration-bulk \
    --fleet-config-file /path/to/fleet_config_file.csv \
    --bulk-assessment-dir /path/to/bulk-assessment-dir \
    --continue-on-error true \
    --start-clean true
```

The following is an example fleet configuration file.

```text
source-db-type,source-db-host,source-db-port,source-db-name,oracle-db-sid,oracle-tns-alias,source-db-user,source-db-password,source-db-schema
oracle,example-host1,1521,ORCL,,,admin,password,schema1
oracle,example-host2,1521,,ORCL_SID,,admin,password,schema2
oracle,,,,,tns_alias,oracle_user,password,schema3
```
