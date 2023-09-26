---
title: fall-forward status reference
headcontent: yb-voyager fall-forward status
linkTitle: fall-forward status
description: YugabyteDB Voyager fall-forward status reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-forward-status
    parent: yb-voyager-cli
    weight: 140
type: docs
---

Shows the status of the fall-forward switchover to the fall-forward database. Status can be "INITIATED", "NOT INITIATED", or "COMPLETED".

#### Syntax

```text
Usage: yb-voyager fall-forward status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-forward switchover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover status. |

#### Example

```sh
yb-voyager fall-forward status --export-dir /path/to/yb/export/dir \
```
