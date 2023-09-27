---
title: analyze schema reference
headcontent: yb-voyager analyze schema
linkTitle: analyze schema
description: YugabyteDB Voyager analyze schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-analyze-schema
    parent: schema-migration
    weight: 20
type: docs
---

[Analyse the PostgreSQL schema](../../migrate/migrate-steps/#analyze-schema) dumped in the export schema step.

#### Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

The valid *arguments* for analyze schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--output-format](#output-format) <format> | One of `html`, `txt`, `json`, or `xml`. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |

#### Example

```sh
yb-voyager analyze-schema --export-dir /path/to/yb/export/dir --output-format txt
```
