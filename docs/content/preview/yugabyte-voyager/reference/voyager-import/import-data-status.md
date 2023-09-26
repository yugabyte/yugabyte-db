---
title: import data status reference
headcontent: yb-voyager import data status
linkTitle: import data status
description: YugabyteDB Voyager import data status reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-data-status
    parent: voyager-import
    weight: 80
type: docs
---

For offline migration, get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

For live migration, get the status report of [import data](#import-data). For live migration with fall forward, the report also includes the status of [fall forward setup](#fall-forward-setup). The report includes the status of tables, the number of rows imported, the total number of changes imported, the number of `INSERT`, `UPDATE`, and `DELETE` events, and the final row count of the target or fall-forward database.

#### Syntax

```text
Usage: yb-voyager import data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [target-db-password](#target-db-password) | Password of the target database. Live migrations only. |
| [ff-db-password](#ff-db-password) | Password of the fall-forward database. Live migration with fall-forward only.  |

#### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```
