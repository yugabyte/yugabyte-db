---
title: Database upgrades in YugabyteDB Aeon
linkTitle: Database upgrade
description: Manage database upgrades for clusters in YugabyteDB Aeon.
headcontent: Upgrade the YugabyteDB software on your cluster
menu:
  preview_yugabyte-cloud:
    identifier: database-upgrade
    parent: cloud-clusters
    weight: 310
type: docs
---

{{< page-finder/head text="Upgrade YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../../manage/upgrade-deployment/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../../yugabyte-platform/manage-deployments/upgrade-software/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" current="" >}}
{{< /page-finder/head >}}

In YugabyteDB Aeon, database upgrades are fully automated and are performed during scheduled [maintenance windows](../cloud-maintenance/).

YugabyteDB Aeon notifies you of upcoming upgrades. When an upgrade is in progress, you have 48 hours to monitor your cluster and then either roll back the upgrade or finalize it. If you take no action, the upgrade is automatically finlized at the end of the monitoring period.

## Caveats

During an upgrade, in addition to the usual cluster [operation locking](../#locking-operations), note the following:

- [Backups](../backup-clusters/)
  - Backups taken on a newer version cannot be restored to clusters running a previous version.
  - Backups taken during the upgrade cannot be restored to clusters running a previous version.
  - Backups taken before the upgrade can be used for restore to the new version.
- [Point-in-time-restore](../aeon-pitr/) (PITR)
  - If you have PITR enabled, you must disable it before performing an upgrade. Re-enable it only after the upgrade is either finalized or rolled back.
  - After the upgrade, PITR cannot be done to a time before the upgrade.
- [xCluster Disaster Recovery](../disaster-recovery/) (DR)
  - While upgradng the DR target, failover is not available.
  - While upgradng the DR source or target, switchover is not available.

## YSQL major upgrade

Upgrading YugabyteDB from a version based on PostgreSQL 11 (v2024.2 and earlier) to a version based on PostgreSQL 15 (v2025.1 or later) requires additional steps.

All DDL statements, except ones related to Temporary table and Refresh Materialized View are blocked for the duration of the upgrade. Consider executing all DDLs before the upgrade, and pause any jobs that might run DDLs. DMLs are allowed.

You should also upgrade your application client drivers to the new version. The client drivers are backwards compatible, and work with both the old and new versions of the database.

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

Use the Pre-Check to make sure your cluster is compatible with the new major YSQL version.

To perform the pre-check, do the following:

1. Navigate to the cluster **Maintenance** tab and .
1. Click **Run Pre-Check**.

If your cluster is not fully compatible with the YSQL major upgrade, the pre-check will fail. Click **View Report** to view a report of recommendations changes.

After a successful upgrade precheck, you can proceed with the usual database upgrade.

## Monitor the universe

Once all the nodes have been upgraded, monitor the universe to ensure it is healthy:

- Make sure workloads are running as expected and there are no errors in the logs.
- Check that all nodes are up and reachable.
- Check the [performance metrics](../../cloud-monitor/overview/) for spikes or anomalies.

If you have problems, you can [roll back](#roll-back-an-upgrade) during this time.

For upgrades that require finalizing, you can monitor for as long as you need, up to a _maximum recommended limit of two days_ to avoid operator errors that can arise from having to maintain two versions.

A subset of features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have finalized the current one.

If you are satisfied with the upgrade:

- For upgrades that do not require finalizing, the upgrade is effectively complete.

- For upgrades that require finalizing, proceed to [Finalize](#finalize-an-upgrade) the upgrade.

## Roll back an upgrade

If you aren't satisfied with an upgrade, you can roll back to the version that was previously installed.

To roll back an upgrade, do the following:

1. Navigate to the cluster **Maintenance** tab.

1. Click **Actions > Roll Back Upgrade** to display the **Roll Back Upgrade** dialog.

    ![Roll back upgrade](/images/yb-platform/upgrade/upgrade-rollback.png)

1. Choose the **Roll back one node at a time** option and set the delay between nodes restarting.

    Select this option to minimize application disruption (at the expense of a longer node-by-node iterative operation). Deselect this option if application downtime is not a concern, and you favor speed; the database cluster is taken offline to perform the upgrade.

1. If you are rolling back one node at a time, specify the delay between node upgrades.

1. Click **Proceed With Rollback**.

YugabyteDB Anywhere starts the rollback process, and you can view the progress on the **Tasks** tab.

## Finalize an upgrade

If your upgrade requires finalizing, the universe has a status of Pending upgrade finalization.

![Finalize upgrade](/images/yb-platform/upgrade/upgrade-finalize.png)

You have the option of [rolling back](#roll-back-an-upgrade), or finalizing. Note that you can't roll back after you finalize.

To finalize an upgrade, do the following:

1. Navigate to **Universes** and select your universe.

1. Click **Finalize Upgrade**.

1. Click **Proceed to finalize the upgrade** to confirm.
