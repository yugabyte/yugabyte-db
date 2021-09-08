---
title: Back up clusters
linkTitle: Back up clusters
description: Back Up clusters in Yugabyte Cloud.
headcontent:
image: /images/section_icons/manage/backup.png
menu:
  latest:
    identifier: backup-clusters
    parent: cloud-clusters
    weight: 500
isTocNested: true
showAsideToc: true
---

The **Backups** tab (paid clusters only) lists the backups that have been run. Yugabyte Cloud performs full cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster. 100GB/month of basic backup storage is provided for every vCPU; more than that and overage charges apply. <!--Refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).-->

By default, every paid cluster is backed up automatically every 24 hours, and these automatic backups are retained for 8 days. The first automatic backup is triggered within the first 10 minutes of creating a table, and scheduled for every 24 hours from the initial backup. 

To change the backup schedule, [create your own schedule](#schedule-backups). You can also perform backups [on demand](#on-demand-backups) and manually [restore backups](#restore-a-backup).

![Cloud Cluster Backups page](/images/yb-cloud/cloud-clusters-backups.png)

To delete a backup, click the Delete icon.

## On demand backups

Typically, you perform on-demand backups before making critical planned changes to the database.

To back up a paid cluster:

1. On the **Backups** tab, click **Backup Now** to display the **Create Backup** dialog.
1. Set the retention period for the backup.
1. Optionally, enter a description of the backup.
1. Click **Backup Now**.

The backup, along with its status, is added to the Backups list.

## Schedule backups

Use scheduled backups to override the default 24 hour/8-day retention policy with your own schedule.

Any changes to the retention policy are applied to new backups only.

To schedule backups for a paid cluster:

1. On the **Backups** tab, click **Policy Settings** to display the **Backup Policy Settings** dialog.
1. Set the retention period for the backup. The maximum retention is 31 days.
1. Set the frequency for the backups.
1. Click **Update Policy**.

## Restore a backup

Before performing a restore, ensure the following:

- the target cluster is sized appropriately; refer to [Scale and configure clusters](../configure-clusters/)
- the target cluster doesn’t have the same namespace(s) as the source cluster

To restore a backup of a paid cluster:

1. On the **Backups** tab, select a backup in the list and click the **Restore Backup** icon to display the **Restore Backup** dialog.
1. Select the backup.
1. Select the target cluster.
1. Click **Restore**.
