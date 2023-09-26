---
title: fall-forward switchover reference
headcontent: yb-voyager fall-forward switchover
linkTitle: fall-forward switchover
description: YugabyteDB Voyager fall-forward switchover reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-forward-switchover
    parent: yb-voyager-cli
    weight: 130
type: docs
---

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