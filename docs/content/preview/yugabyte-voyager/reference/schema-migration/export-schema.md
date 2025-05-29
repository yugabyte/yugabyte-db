---
title: export schema reference
headcontent: yb-voyager export schema
linkTitle: export schema
description: YugabyteDB Voyager export schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-schema
    parent: schema-migration
    weight: 10
type: docs
---

[Export the schema](../../../migrate/migrate-steps/#export-and-analyze-schema) from the source database.

## Syntax

```text
Usage: yb-voyager export schema [ <arguments> ... ]
```

### Arguments

The following table lists the valid flags and parameters for the `export schema` command.

`export schema` parameters are under `export-schema:` in the configuration file.

When run at the same time, flags take precedence over configuration flag settings.

| <div style="width:150px">Flag</div> | <div style="width:150px">Parameter</div> | Description |
| :------- | :------------------------ | :------------------------ |
| --assessment-report-path | `assessment-report-path:` | Path to the generated assessment report file (JSON format) to be used for applying recommendation to exported schema. |
| --skip-recommendations | `skip-recommendations:` | Disable applying recommendations in the exported schema suggested by the migration assessment report. <br> Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --comments-on-objects | `comments-on-objects:` | Enable export of comments associated with database objects. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --object-type-list, <br> --exclude-object-type-list  | `object-type-list:`<br>`exclude-object-type-list:` | Comma-separated list of objects to export (--object-type-list) or not (--exclude-object-type-list). You can provide only one of the arguments at a time. <br> Example: `yb-voyager export schema …. -object-type-list "TABLE,FUNCTION,VIEW"` <br> Accepted parameters: <ul><li>Oracle: TYPE, SEQUENCE, TABLE, PACKAGE, TRIGGER, FUNCTION, PROCEDURE, SYNONYM, VIEW, MVIEW </li><li>PostgreSQL: TYPE, DOMAIN, SEQUENCE, TABLE, FUNCTION, PROCEDURE, AGGREGATE, VIEW, MVIEW, TRIGGER, COMMENT</li><li>MySQL: TABLE, VIEW, TRIGGER, FUNCTION, PROCEDURE</li></ul> |
| --use-orafce | `use-orafce:` | Use the Orafce extension. Oracle migrations only. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | `export-dir:` | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| --send-diagnostics | `send-diagnostics:` | Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --run-guardrails-checks | `run-guardrails-checks:` | Run guardrails checks during migration. <br>Default: true<br>Accepted values: true, false, yes, no, 0, 1 |
| --start-clean | — | Starts a fresh schema export after clearing the `schema` directory.<br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -h, --help | — | Command line help for schema. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Source database arguments

The following table lists the valid source database flags and parameters for the `export schema` command.

Source database parameters are specified under `source:` in the configuration file.

| <div style="width:150px">Flag</div> | <div style="width:150px">Parameter</div> | Description |
| :------- | :------------------------ | :------------------------ |
| --source-db-type | `db-type:` | One of `postgresql`, `mysql`, or `oracle`. |
| --source-db-host | `db-host:` | Domain name or IP address of the machine on which the source database server is running. |
| --source-db-port | `db-port:` | Port number of the source database server. |
| --source-db-name | `db-name:` | Source database name. |
| --source-db-schema | `db-schema:` | Schema name of the source database. Not applicable for MySQL. |
| --source-db-user | `db-user:` | Name of the source database user (typically `ybvoyager`). |
| --source-db-password | `db-password:` | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) | `ssl-mode:` | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) | `ssl-cert:` | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) | `ssl-key:` | Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) | `ssl-crl:` | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) | `ssl-root-cert:` | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --oracle-home <path> | `oracle-home:` | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#oracle-options) <alias> | `oracle-tns-alias:` | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --oracle-db-sid <SID> | `oracle-db-sid:` | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|

### Example

Configuration file:

```yaml
yb-voyager export schema --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager export schema --export-dir /dir/export-dir \
        --source-db-type oracle \
        --source-db-host 127.0.0.1 \
        --source-db-port 1521 \
        --source-db-user ybvoyager \
        --source-db-password 'password'  \
        --source-db-name source_db \
        --source-db-schema source_schema \
        --start-clean true
```
