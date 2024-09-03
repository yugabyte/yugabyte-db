---
title: assess migration reference
headcontent: yb-voyager assess-migration
linkTitle: Assess migration
description: YugabyteDB Voyager assess migration reference
menu:
  preview_yugabyte-voyager:
    identifier: assess-migration-voyager
    parent: yb-voyager-cli
    weight: 1
badges: tp
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

| Argument | Description/valid options |
| :------- | :------------------------ |
| &#8209;&#8209;assessment&#8209;metadata&#8209;dir | Directory path where assessment metadata like source database metadata and statistics are stored. Optional flag, if not provided, it will be assumed to be present at default path inside the export directory. |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --iops-capture-interval | Interval (in seconds) at which Voyager will gather IOPS metadata from the source database for the given schema(s). <br> Default: 120 |
| --oracle-db-sid | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only. |
| --oracle-home | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only. |
| [--oracle-tns-alias](../yb-voyager-cli/#oracle-options) | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --source-db-host <hostname> | Domain name or IP address of the machine on which the source database server is running. <br>Default: localhost |
| --source-db-name | Source database name. |
| &#8209;&#8209;source&#8209;db&#8209;password | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port | Source database server port number. <br>Default: PostgreSQL (5432), Oracle (1521) |
| --source-db-schema | Source schema name(s) to export. In case of PostgreSQL, it can be single or a comma-separated list of schemas: `schema1,schema2,schema3`. |
| --source-db-type |  Source database type (postgresql, oracle). |
| --source-db-user |  Username of the source database. |
| [--source-ssl-cert](../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../yb-voyager-cli/#ssl-connectivity) | Path of the file containing source SSL Root Certificate Revocation List (CRL). |
| [--source-ssl-key](../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the key which is part of the SSL `<cert,key>` pair.|
| [--source-ssl-mode](../yb-voyager-cli/#ssl-connectivity) | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../yb-voyager-cli/#ssl-connectivity) | Path of the file containing source SSL Root Certificate. |
| --start-clean | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |
| -y, --yes | Assume answer to all prompts during migration. <br>Default: false |

### Example

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

| Argument | Description/valid options |
| :------- | :------------------------ |
| &#8209;&#8209;bulk&#8209;assessment&#8209;dir | Directory path where assessment data is output. |
| --continue-on-error | Print the error message to the console and continue to next schema assessment.<br>Default: true.<br>Accepted parameters: true, false, yes, no, 0, 1. |
| --fleet-config-file | Path to the CSV file with connection parameters for schema(s) to be assessed. The first line must be a header row. <br>Fields (case-insensitive): 'source-db-type', 'source-db-host', 'source-db-port', 'source-db-name', 'oracle-db-sid', 'oracle-tns-alias', 'source-db-user', 'source-db-password', 'source-db-schema'.<br>Mandatory: 'source-db-type', 'source-db-user', 'source-db-schema', and one of ['source-db-name', 'oracle-db-sid', 'oracle-tns-alias']. |
| -h, --help | Command line help. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |
| -y, --yes | Assume answer to all prompts during migration. <br>Default: false |

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
