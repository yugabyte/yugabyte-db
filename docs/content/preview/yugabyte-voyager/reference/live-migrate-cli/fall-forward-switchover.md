---
title: fall-forward switchover reference
headcontent: yb-voyager fall-forward switchover
linkTitle: fall-forward switchover
description: YugabyteDB Voyager fall-forward switchover reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-forward-switchover
    parent: live-migrate-cli
    weight: 130
type: docs
---

### fall-forward switchover

Initiate a switchover to the fall-forward database.

#### Syntax

```text
Usage: yb-voyager fall-forward switchover [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-forward switchover are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for switchover. |

#### Example

```sh
yb-voyager fall-forward switchover --export-dir /path/to/yb/export/dir
```

### fall-forward status

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
