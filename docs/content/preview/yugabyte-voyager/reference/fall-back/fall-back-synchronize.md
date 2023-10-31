---
title: fall-back synchronize reference
headcontent: yb-voyager fall-back synchronize
linkTitle: fall-back synchronize
description: YugabyteDB Voyager fall-back synchronize reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-back-synchronize
    parent: fall-back
    weight: 100
type: docs
---

Exports new changes from the target YugabyteDB database to import to the fall-back database so that the fall-back database can be in sync with the YugabyteDB database after cutover.

## Syntax

```text
Usage: yb-voyager fall-back synchronize [ <arguments> ... ]
```

### Arguments

The valid *arguments* for fall-back synchronize are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb | Use this argument to disable progress bar during data export and printing statistics during the streaming phase. (default false).<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --exclude-table-list <tableNames> | Comma-separated list of tables to exclude while exporting data. <br> Table names can include glob wildcard characters `?` (matches one character) and `*` (matches zero or more characters). In case the table names are case sensitive, double-quote them. For example, `--exclude-table-list 'orders,"Products",items'`. |
| --exclude-table-list-file-path | Path of the file containing comma-separated list of table names to exclude while exporting data. |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for synchronize. |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list | Comma-separated list of the table names to export data. Table names can include glob wildcard characters `?` (matches one character) and `*` (matches zero or more characters). In case the table names are case sensitive, double-quote them. For example `--table-list 'orders,"Products",items'`. |
| --table-list-file-path | Path to the file containing comma-separated list of table names to export data. |
| --target-db-password <password>| Password to connect to the target YugabyteDB server. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --verbose | Display extra information in the output. (default: false) <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes| Answer yes to all prompts during migration. (default: false) |

### Example

```sh
yb-voyager fall-back synchronize --export-dir /dir/export-dir \
        --target-db-password 'password' \
```
