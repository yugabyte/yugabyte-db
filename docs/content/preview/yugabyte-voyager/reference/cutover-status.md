---
title: cutover status reference
headcontent: yb-voyager cutover status
linkTitle: cutover status
description: YugabyteDB Voyager cutover status reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-cutover-status
    parent: yb-voyager-cli
    weight: 120
type: docs
---

Shows the status of the cutover to the YugabyteDB database. Status can be "INITIATED", "NOT INITIATED", or "COMPLETED".

#### Syntax

```text
Usage: yb-voyager cutover status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for cutover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover status. |

#### Example

```sh
yb-voyager cutover status --export-dir /path/to/yb/export/dir
```
