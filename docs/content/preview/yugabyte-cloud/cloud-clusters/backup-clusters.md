---
title: Backup and restore clusters
linkTitle: Backup and restore
description: Back up and restore clusters in YugabyteDB Managed.
headcontent: Configure your backup schedule and restore databases
image: /images/section_icons/manage/backup.png
menu:
  preview_yugabyte-cloud:
    identifier: backup-clusters
    parent: cloud-clusters
    weight: 200
type: docs
---

YugabyteDB Managed performs full cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster. 100GB/month of basic backup storage is provided for every vCPU; more than that and overage charges apply. Refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

By default, clusters are backed up automatically every 24 hours, and these automatic backups are retained for 8 days. The first automatic backup is triggered after 24 hours of creating a table, and is scheduled every 24 hours thereafter.

Back up and restore clusters, configure the automatic backup policy, and review previous backups and restores using the cluster **Backups** tab.

To change the backup schedule, [create your own schedule](#schedule-backups). To enable or disable scheduled backups, click the **Scheduled backup** option.

You can also perform backups [on demand](#on-demand-backups) and manually [restore backups](#restore-a-backup).

Backups are not supported for Sandbox clusters.

![Cluster Backups page](/images/yb-cloud/cloud-clusters-backups.png)

To delete a backup, click the **Delete** icon.

To review previous backups, click **Backup**. To review previous restores, click **Restore**.

## On demand backups

Typically, you perform on-demand backups before making critical planned changes to the database.

To back up a cluster:

1. On the **Backups** tab, click **Backup Now** to display the **Create Backup** dialog.
1. Set the retention period for the backup.
1. Optionally, enter a description of the backup.
1. Click **Backup Now**.

The backup, along with its status, is added to the Backups list.

## Schedule backups

Use scheduled backups to override the default 24 hour/8-day retention policy with your own schedule.

Any changes to the retention policy are applied to new backups only.

To schedule backups for a cluster:

1. On the **Backups** tab, click **Policy Settings** to display the **Backup Policy Settings** dialog.
1. Set the retention period for the backup. The maximum retention is 31 days.
1. Choose **Simple** to set the frequency for the backups. Choose **Custom** to select the days of the week to run backups, along with the start time.
1. Click **Update Policy**.

## Restore a backup

Before performing a restore, ensure the following:

- the target cluster is sized appropriately; refer to [Scale and configure clusters](../configure-clusters/)
- the target cluster doesn't have the same namespaces as the source cluster

To review previous restores, click **Restore**.

To restore a backup of a cluster:

1. On the **Backups** tab, select a backup in the list and click the **Restore** icon to display the **Restore Backup** dialog.
1. Select the target cluster.
1. Click **Restore**.
