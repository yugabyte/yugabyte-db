---
title: Assess migration
headcontent: yb-voyager assess-migration
linkTitle: Assess migration
description: YugabyteDB Voyager assess migration reference
menu:
  preview_yugabyte-voyager:
    identifier: assess-migration-voyager
    parent: yb-voyager-cli
    weight: 1
techPreview: /preview/releases/versioning/#feature-availability
type: docs
---

[Assess the migration](../../migrate/assess-migration) from source (PostgreSQL) database to YugabyteDB.

## Syntax

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
| --send-diagnostics  | Enable or disable sending [diagnostics](../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --source-db-host <hostname> | Domain name or IP address of the machine on which the source database server is running. <br>Default: localhost |
| --source-db-name | Source database name. |
| &#8209;&#8209;source&#8209;db&#8209;password | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port | Source database server port number. <br>Default: PostgreSQL(5432) |
| --source-db-schema | Source schema name(s) to export. In case of multiple schemas, use a comma-separated list of schemas: `schema1,schema2,schema3`. |
| --source-db-type |  Source database type (postgresql). |
| --source-db-user |  Username of the source database. |
| [--source-ssl-cert](../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../yb-voyager-cli/#ssl-connectivity) | Path of the file containing source SSL Root Certificate Revocation List (CRL). |
| [--source-ssl-key](../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the key which is part of the SSL `<cert,key>` pair.|
| [--source-ssl-mode](../yb-voyager-cli/#ssl-connectivity) | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../yb-voyager-cli/#ssl-connectivity) | Path of the file containing source SSL Root Certificate. |
| --start-clean | Cleans up the project directory for schema or data files depending on the export command. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |
| -y, --yes | Assume answer to all prompts during migration. <br>Default: false |

## Example

```sh
yb-voyager assess-migration --source-db-type postgresql \
        --source-db-host hostname --source-db-user username \
        --source-db-password password --source-db-name dbname \
        --source-db-schema schema1,schema2 --export-dir /path/to/export/dir
```
