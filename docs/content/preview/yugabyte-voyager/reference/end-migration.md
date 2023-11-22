---
title: End migration
linkTitle: End migration
description: YugabyteDB Voyager end migration reference
menu:
  preview_yugabyte-voyager:
    identifier: end-migration-voyager
    parent: yb-voyager-cli
    weight: 100
type: docs
---

Cleans up all the migration-related information and metadata stored in the export directory (export-dir) and databases (source, target, and source-replica). The command also provides an option to back up the schema, data, migration reports and log files.

## Syntax

```text
Usage: yb-voyager end migration [ <arguments> ... ]
```

### Arguments

The valid *arguments* for end migration are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --backup-data-files | Back up snapshot data files. <br>Accepted parameters: true, false, yes, no, 0, 1 |
| --backup-log-files | Back up yb-voyager log files for the current migration. <br>Accepted parameters: true, false, yes, no, 0, 1 |
| --backup-schema-files | Back up migration schema files. <br>Accepted parameters: true, false, yes, no, 0, 1 |
| --save-migration-reports | Saves all the reports generated in the migration workflow (`analyze-schema` report, `export data status` output, `import data status` output, or `get data-migration-report`). <br>Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --backup-dir | A directory where all the back up files of schema, data, logs, and reports are saved.<br> **Note** that this argument is mandatory only if any of these flags are set to true or yes or 1:  `--backup-data-files`,  `--backup-log-files`,  `--backup-schema-files`, `--save-migration-reports`. |
| -h, --help | Command line help for end migration. |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Example

```sh
yb-voyager end migration --export-dir /dir/export-dir
        --backup-log-files true
        --backup-data-files true
        --backup-schema-files true
        --save-migration-reports true
        --backup-dir /dir/backup-dir
```
