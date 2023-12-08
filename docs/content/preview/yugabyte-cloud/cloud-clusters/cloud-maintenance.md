---
title: Maintenance windows
linkTitle: Maintenance windows
description: Manage maintenance windows for clusters in YugabyteDB Managed.
headcontent: Manage cluster maintenance windows and set exclusion periods
menu:
  preview_yugabyte-cloud:
    identifier: cloud-maintenance
    parent: cloud-clusters
    weight: 300
type: docs
---

Yugabyte occasionally performs maintenance on clusters. This can include infrastructure and database upgrades. Depending on the type of maintenance, your cluster may be restarted, which disrupts service briefly. Yugabyte notifies you in advance of any upcoming maintenance via email. The email includes the date and time of the maintenance window. One week before a scheduled maintenance, an **Upcoming Maintenance** badge is displayed on the cluster.

Yugabyte only performs cluster maintenance, including YugabyteDB database upgrades and server setting updates, during scheduled maintenance windows. The maintenance window is a weekly four hour interval during which Yugabyte may perform maintenance on the cluster.

You can manage when maintenance is done on Dedicated clusters (these features are not available for Sandbox clusters) in the following ways:

- [Set the maintenance window schedule](#set-the-cluster-maintenance-window-schedule).
- [Schedule exclusion periods](#set-a-maintenance-exclusion-period), during which Yugabyte won't perform maintenance; any scheduled maintenance is delayed until the next maintenance window after the exclusion period. [Critical maintenance](#critical-maintenance) events override exclusion periods.
- Delay a scheduled maintenance to the next available window. You can't delay a scheduled maintenance more than 7 days in advance.

You set the cluster maintenance window, exclusion periods, and review upcoming maintenance events using the cluster **Maintenance** tab.

![Cluster Maintenance page](/images/yb-cloud/cloud-clusters-maintenance.png)

To view details of upcoming scheduled maintenance, click the **Upcoming Maintenance** badge, or select the maintenance in the **Scheduled Maintenance** list to display the **Maintenance Details**.

To delay a scheduled maintenance, click **Delay to next available window** on the **Maintenance Details** sheet.

If the scheduled maintenance is a database upgrade, you can start the upgrade by clicking **Upgrade Now** on the **Maintenance Details** sheet.

## Recommendations

Maintenance operations, including database upgrades, certificate rotations, and cluster maintenance, block other cluster operations such as backups, and incur a load on the cluster.

- Avoid scheduling during [scheduled backups](../backup-clusters/).
- Schedule the window for low traffic periods to reduce the impact of rolling updates.
- If you have a [staging environment](../../cloud-basics/create-clusters-overview/#staging-cluster), schedule the maintenance window for the staging cluster to a time before that of the production cluster, so that you can validate updates against your applications in your pre-production environment _before_ updating your production cluster. You can also set an exclusion period for the production cluster.

Note that if another [locking cluster operation](../#locking-operations) is already running, the maintenance operation must wait for it to finish. A scheduled maintenance will continue to attempt to run while the maintenance window is open, and if it cannot run, is postponed to the next available window.

## Set the cluster maintenance window schedule

To set the maintenance window for a cluster:

1. On the **Maintenance** tab, click **Edit Maintenance Preferences** to display the **Maintenance Preferences** dialog.
1. Choose a day of the week.
1. Set the start time.
1. Click **Save**.

## Set a maintenance exclusion period

To set the maintenance exclusion period for a cluster:

1. On the **Maintenance** tab, click **Edit Maintenance Preferences** to display the **Maintenance Preferences** dialog.
1. Set a start date and end date. The exclusion period includes the day of the start date, and every day up to, but not including, the end date.
1. Click **Save**.

## What to expect during maintenance

Yugabyte performs rolling maintenance and upgrades on multi-node clusters with zero downtime. However, the cluster is still subject to the following:

- Dropped connections - Connections to the stopped node are dropped. Verify your connection pool, driver, and application to ensure they handle dropped connections correctly. Any failures need to be retried.
- Less bandwidth - During maintenance, traffic is diverted to the running nodes. To mitigate this, set your maintenance window to a low traffic period. You can also add nodes (scale out) prior to the upgrade.
- Not [highly available](../../../architecture/core-functions/high-availability/) - During maintenance, one node is always offline. In a multi-node cluster (that is, with a [replication factor](../../../architecture/docdb-replication/replication/) of 3), if one of the remaining nodes goes down, you can no longer access the database.

## Critical maintenance

Yugabyte occasionally performs critical maintenance on clusters. This includes routine but time-sensitive maintenance and updates. As with regular maintenance, Yugabyte notifies you in advance of any upcoming critical maintenance via email.

Critical maintenance includes the following:

- monthly node operating system updates, package updates, and security patches
- rotating SSL certificates used for encrypting communication between nodes

Critical maintenance is performed during the next scheduled maintenance window.

Critical maintenance events also override any exclusion periods, can't be delayed, and take precedence over any already scheduled regular maintenance.
