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

The following table lists the valid CLI flags for the `export schema` command, along with their corresponding config file parameters.

_Note: All the configuration file parameters are under `export-schema:` and can be overridden by their corresponding CLI flags._

| <div style="width:150px">CLI flags</div> | Configuration parameters | Description/valid options |
| :------- | :------------------------ | :------------------------ |
| --assessment-report-path | `assessment-report-path:` | Path to the generated assessment report file (JSON format) to be used for applying recommendation to exported schema. |
| --skip-recommendations | `skip-recommendations:` | Disable applying recommendations in the exported schema suggested by the migration assessment report. <br> Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --comments-on-objects | `comments-on-objects:` | Enable export of comments associated with database objects. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --object-type-list, <br> --exclude-object-type-list  | `object-type-list:`<br>`exclude-object-type-list:` | Comma-separated list of objects to export (--object-type-list) or not (--exclude-object-type-list). You can provide only one of the arguments at a time. <br> Example: `yb-voyager export schema …. -object-type-list "TABLE,FUNCTION,VIEW"` <br> Accepted parameters: <ul><li>Oracle: TYPE, SEQUENCE, TABLE, PACKAGE, TRIGGER, FUNCTION, PROCEDURE, SYNONYM, VIEW, MVIEW </li><li>PostgreSQL: TYPE, DOMAIN, SEQUENCE, TABLE, FUNCTION, PROCEDURE, AGGREGATE, VIEW, MVIEW, TRIGGER, COMMENT</li><li>MySQL: TABLE, VIEW, TRIGGER, FUNCTION, PROCEDURE</li></ul> |
| --use-orafce | `use-orafce:` | Use the Orafce extension. Oracle migrations only. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | `export-dir:` | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| --send-diagnostics | `send-diagnostics:` | Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --run-guardrails-checks | `run-guardrails-checks:` | Run guardrails checks during migration. <br>Default: true<br>Accepted values: true, false, yes, no, 0, 1 |
| --source-db-type | `source: db-type:` | One of `postgresql`, `mysql`, or `oracle`. |
| --source-db-host | `source: db-host:` | Domain name or IP address of the machine on which the source database server is running. |
| --source-db-port | `source: db-port:` | Port number of the source database server. |
| --source-db-name | `source: db-name:` | Source database name. |
| --source-db-schema | `source: db-schema:` | Schema name of the source database. Not applicable for MySQL. |
| --source-db-user | `source: db-user:` | Name of the source database user (typically `ybvoyager`). |
| --source-db-password | `source: db-password:` | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) | `source: ssl-mode:` | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) | `source: ssl-cert:` | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) | `source: ssl-key:` | Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) | `source: ssl-crl:` | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) | `source: ssl-root-cert:` | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --oracle-home <path> | `source: oracle-home:` | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#oracle-options) <alias> | `source: oracle-tns-alias:` | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --oracle-db-sid <SID> | `source: oracle-db-sid:` | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|
| --start-clean | — | Starts a fresh schema export after clearing the `schema` directory.<br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -h, --help | — | Command line help for schema. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |


### Example

Config file example:

```yaml
yb-voyager export schema --config-file <path-to-config-file>
```

CLI example:

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
