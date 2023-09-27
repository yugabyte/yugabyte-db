---
title: archive changes reference
headcontent: yb-voyager archive changes
linkTitle: archive changes
description: YugabyteDB Voyager archive changes reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-archive-changes
    parent: live-migrate-cli
    weight: 150
type: docs
---

Archives the streaming data from the source database.

#### Syntax

```text
Usage: yb-voyager archive changes [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for archive changes status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for archive changes. |
| [--delete](#delete) |  Delete exported data after moving it to the target database. (Default: false) |
| [--move-to](#move-to) <path> | Destination path to move exported data to. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. (Default: true) |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during migration (Default: false). |

