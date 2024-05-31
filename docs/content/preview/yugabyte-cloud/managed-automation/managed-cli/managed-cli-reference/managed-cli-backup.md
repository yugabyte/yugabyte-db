---
title: ybm CLI backup resource
headerTitle: ybm backup
linkTitle: backup
description: YugabyteDB Managed CLI reference backup resource.
headcontent: Manage cluster backups
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-backup
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `backup` resource to perform operations on cluster backups, including the following:

- create and delete cluster backups
- restore a backup
- get information about cluster backups

## Syntax

```text
Usage: ybm backup [command] [flags]
```

## Example

Create a backup:

```sh
ybm backup create \
    --cluster-name=test-cluster \
    --retention-period=7
```

## Commands

### create

Create a backup of a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to back up. |
| --retention-period | Retention period for the backup in days. Default is 1. |
| --description | A description of the backup. |

### delete

Delete a specified backup.

| Flag | Description |
| :--- | :--- |
| --backup-id | Required. The ID of the backup to delete. |

### list

Display the backups of a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Name of the cluster of which you want to view the backups. |

### restore

Restore a specified backup to a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to restore to. |
| --backup-id | Required. The ID of the backup to restore. |
