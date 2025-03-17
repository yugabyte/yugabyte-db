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

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| --output-format | Format in which the report file is generated. One of `html`, `txt`, `json`, or `xml`. If not provided, reports are generated in both `json` and `html` formats by default. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --target-db-version | Specifies the target version of YugabyteDB in the format `A.B.C.D`.<br>Default: latest stable version |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |

### Example

```sh
yb-voyager analyze-schema --export-dir /dir/export-dir --output-format txt
```
