---
title: cutover initiate reference
headcontent: yb-voyager cutover initiate
linkTitle: cutover initiate
description: YugabyteDB Voyager cutover initiate reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-cutover-initiate
    parent: live-migrate-cli
    weight: 110
type: docs
---

### cutover initiate

Initiate cutover to the YugabyteDB database.

#### Syntax

```text
Usage: yb-voyager cutover initiate [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for cutover initiate are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover initiate. |

#### Example

```sh
yb-voyager cutover initiate --export-dir /path/to/yb/export/dir
```

### cutover status

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