---
title: fall-back setup reference
headcontent: yb-voyager fall-back setup
linkTitle: fall-back setup
description: YugabyteDB Voyager fall-back setup reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-back-setup
    parent: fall-back
    weight: 90
type: docs
---

Imports data to the fall-back database, and streams new changes from the YugabyteDB database to the fall-back database.

### Syntax

```text
Usage: yb-voyager fall-back setup [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-back setup are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for setup. |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)<br> Accepted parameters: true, false, yes, no, 0, 1|
| --source-db-password | Password to connect to the source database server. |
| --start-clean |  Starts a fresh import with exported data files present in the export-dir/data directory. <br> If any table on YugabyteDB database is non-empty, it prompts whether you want to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files with upsert mode. <br>Note that for the cases where a table doesn't have a primary key, this may lead to insertion of duplicate data. To avoid this, exclude the table using the --exclude-file-list or truncate those tables manually before using the start-clean flag. <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --verbose | Display extra information in the output. (default: false) <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the migration. (default: false) |

### Example

```sh
yb-voyager fall-back setup --export-dir /dir/export-dir \
        --source-db-password 'password'
```
