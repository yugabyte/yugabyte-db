---
title: export data reference
headcontent: yb-voyager export data status
linkTitle: export-data
description: YugabyteDB Voyager export data status reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-data-status
    parent: yb-voyager-cli
    weight: 40
type: docs
---

### export data status

For offline migration, get the status report of an ongoing or completed data export operation.

For live migration (and fall-forward), get the report of the ongoing export phase which includes metrics such as the number of rows exported in the snapshot phase, the total number of change events exported from the source, the number of `INSERT`/`UPDATE`/`DELETE` events, and the final row count exported.

#### Syntax

```text
Usage: yb-voyager export data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for export data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |

#### Example

```sh
yb-voyager export data status --export-dir /path/to/yb/export/dir
```

