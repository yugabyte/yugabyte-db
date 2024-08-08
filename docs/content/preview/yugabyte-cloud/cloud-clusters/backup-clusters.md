---
title: Back up and restore clusters
linkTitle: Backup and restore
description: Back up and restore clusters in YugabyteDB Aeon.
headcontent: Configure your backup schedule and restore databases
menu:
  preview_yugabyte-cloud:
    identifier: backup-clusters
    parent: cloud-clusters
    weight: 200
type: docs
---

YugabyteDB Aeon can perform full and incremental cluster (all namespaces) level backups, and the backups are stored in the same region as your cluster.

{{< youtube id="3qzAdrVFgxc" title="Back up and restore clusters in YugabyteDB Aeon" >}}

By default, clusters are backed up automatically every 24 hours, and these automatic full backups are retained for 8 days. The first automatic backup is triggered after 24 hours of creating a table, and is scheduled every 24 hours thereafter.

Full backups are a complete copy of the cluster. You can additionally schedule incremental backups between full backups. Incremental backups only include the data that has changed since the last backup, be it a full or incremental backup. Incremental backups provide the following advantages:

- Faster - Incremental backups are much quicker as they deal with a smaller amount of data.
- Reduced storage - Because only the delta from previous backup is captured, incremental backups consume less storage space compared to full backups.
- Higher frequency - Incremental backups can be scheduled at smaller intervals (down to hourly), providing a lower recovery point objective (RPO).

Back up and restore clusters, configure the scheduled backup policy, and review previous backups and restores using the cluster **Backups** tab.

To change the backup schedule, [create your own schedule](#manage-scheduled-backups).

You can also perform backups [on demand](#on-demand-backups) and manually [restore backups](#restore-a-backup).

![Cluster Backups page](/images/yb-cloud/cloud-clusters-backups.png)

To delete a backup, click the **Delete** icon.

To review previous backups, click **Backup**. To review previous restores, click **Restore**.

## Location of backups

Backups are located in cloud storage of the provider where the cluster is deployed. The storage is located is the same region os the cluster. For example, for a cluster deployed in AWS and located in us-east-2, backups are stored in an S3 bucket in us-east-2.

For [Replicate across region](../../cloud-basics/create-clusters-topology/#replicate-across-regions) clusters, the backup is stored in one of the cluster regions, as determined automatically by Aeon when the cluster is created.

For [Partition by region](../../cloud-basics/create-clusters-topology/#partition-by-region) clusters, the database schema and tablet details are stored in the primary region, and the regional tablespace data is stored in its respective region to preserve data residency.

## Limitations

If [some cluster operations](../#locking-operations) are already running during a scheduled backup window, the backup may be prevented from running.

Backups that don't run are postponed until the next scheduled backup. You can also perform a manual backup after the blocking operation completes.

You can't restore a backup to a cluster with an version of YugabyteDB that is earlier than that of the backup. If you need to restore to an earlier version, contact {{% support-cloud %}}.

Backups are not supported for Sandbox clusters.

## Recommendations

- Don't perform cluster operations at the same time as your scheduled backup.
- Configure your [maintenance window](../cloud-maintenance/) and [backup schedule](#manage-scheduled-backups) so that they do not conflict.
- Performing a backup or restore incurs a load on the cluster. Perform backup operations when the cluster isn't experiencing heavy traffic. Backing up during times of heavy traffic can temporarily degrade application performance and increase the length of time of the backup.
- Avoid running a backup during or before a scheduled maintenance.

## On demand backups

Typically, you perform on-demand backups before making critical planned changes to the database.

To back up a cluster:

1. On the **Backups** tab, click **Backup Now** to display the **Create Backup** dialog.
1. Set the retention period for the backup.
1. Optionally, enter a description of the backup.
1. Click **Backup Now**.

The backup, along with its status, is added to the Backups list.

## Manage scheduled backups

Use a backup policy to schedule backups and override the default 24 hour/8-day retention policy.

Any changes to the retention policy are applied to new backups only.

To manage the cluster backup policy, do the following:

1. On the **Backups** tab, click **Scheduled Backup Settings** and choose **Edit Backup Policy** to display the **Backup Policy** dialog.
1. Specify how often to take full backups of the database.
1. To take incremental backups, select the **Enable incremental backup** option and specify how often to take incremental backups.
1. Set the retention period for the backup. The maximum retention is 31 days.
1. Click **Save**.

To disable the scheduled backup, click **Scheduled Backup Settings** and choose **Disable Scheduled Backup**. Click **Enable Scheduled Backup** to re-enable the policy.

## Restore a backup

Before performing a restore, ensure the following:

- the target cluster is sized appropriately; refer to [Scale and configure clusters](../configure-clusters/)
- if the target cluster has the same namespaces as the source cluster, those namespaces don't have any tables
- the target cluster has the same users as the source cluster. If you restore to a cluster where some users are missing, ownership of objects owned by missing users defaults to `yugabyte`, and you must contact {{% support-cloud %}} to reset the owners.

To review previous restores, on the **Backups** tab, select **Restore History**.

To restore a backup of a cluster:

1. On the **Backups** tab, select a backup in the list to display the **Backup Details** sheet.
1. Click **Restore** to display the **Restore Backup** dialog.
1. Choose the databases or keyspaces to restore and click **Next**.
1. Select the target cluster.
1. Click **Restore Now**.
