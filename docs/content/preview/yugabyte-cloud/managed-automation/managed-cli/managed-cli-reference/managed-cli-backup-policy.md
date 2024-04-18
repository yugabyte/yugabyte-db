---
title: ybm CLI backup policy resource
headerTitle: ybm backup policy
linkTitle: backup policy
description: YugabyteDB Managed CLI reference backup policy resource.
headcontent: Manage cluster backup policies
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-backup-policy
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `backup policy` resource to change the cluster backup policy, including the following:

- update the cluster backup policy
- enable and disable the backup policy
- display the cluster backup policy

The default policy for clusters is to back up every day with 8-day retention.

## Syntax

```text
Usage: ybm backup policy [command] [flags]
```

## Example

Modify the cluster backup policy:

```sh
ybm backup policy update \
    --cluster-name=test-cluster \
    --full-backup-frequency-in-days=2 \
    --retention-period-in-days=14
```

## Commands

### disable

Disable the backup policy for a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to disable the backup policy for. |

### enable

Enable the backup policy for a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to enable the backup policy for. |

### list

Display the backup policy of a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster whose policy you want to view. |

### update

Update the backup policy of a specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster to update the backup policy for. |
| --full-backup-frequency-in-days | Frequency of full backup in days. Default is 1. |
| --full-backup-schedule-days-of-week | Days of the week to run backups. Specify a comma-separated list of the first two letters of the days. For example, MO,WE,SA. |
| --full-backup-schedule-time | Time of day to run backups. Specify local time in 24 hr HH:MM format. For example, 15:04. |
| --incremental-backup-frequency-in-minutes | Frequency of incremental backups in minutes. Default is 60. |
| --retention-period-in-days | Required. Retention period for the backup in days. Default is 1. |
