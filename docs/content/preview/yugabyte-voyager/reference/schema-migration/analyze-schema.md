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

[Analyse the PostgreSQL schema](../../../migrate/migrate-steps/#analyze-schema) dumped in the export schema step.

## Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

### Arguments

The valid *arguments* for analyze schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| --output-format <format> | Format in which the report file is generated. One of `html`, `txt`, `json`, or `xml`. |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true) |
| --verbose | Display extra information in the output. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |

#### Example

```sh
yb-voyager analyze-schema --export-dir /dir/export-dir --output-format txt
```
