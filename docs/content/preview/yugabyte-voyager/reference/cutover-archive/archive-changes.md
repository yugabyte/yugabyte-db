---
title: archive changes reference
headcontent: yb-voyager archive changes
linkTitle: archive changes
description: YugabyteDB Voyager archive changes reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-archive-changes
    parent: cutover-archive
    weight: 150
type: docs
---

The archive changes command limits the disk space used by the locally queued CDC events. After the changes from the local queue are applied on the target YugabyteDB database (and source-replica database), they are eligible for deletion. The command gives an option to archive the changes before deleting by moving them to another directory.

Note that even if some changes are applied to the target databases, locally queued CDC events are deleted only after the disk use exceeds 70%.

## Syntax

```text
Usage: yb-voyager archive changes [ <arguments> ... ]
```

### Arguments

The valid *arguments* for archive changes are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| &#8209;&#8209;delete&#8209;changes&#8209;without&#8209;archiving | Delete the imported changes without archiving them. Note that the changes are deleted from the export directory only after disk utilisation exceeds 70%. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --fs-utilization-threshold | Disk utilization threshold in percentage. <br>Default: 70 |
| -h, --help | Command line help for archive changes. |
| --move-to | Path to the directory where the imported change events are to be moved to. Note that the changes are deleted from the export directory only after the disk use exceeds 70%. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during migration. <br>Default: false |

### Examples

Example to delete changes without archiving them to another destination is as follows:

```sh
yb-voyager archive changes --export-dir /dir/export-dir --delete-changes-without-archiving true
```

Example to archive changes from the export directory to another destination is as follows:

```sh
yb-voyager archive changes --export-dir /dir/export-dir --move-to /dir/archived-changes-dir
```
