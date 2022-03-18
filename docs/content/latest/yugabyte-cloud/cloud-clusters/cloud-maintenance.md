---
title: Maintenance windows
linkTitle: Maintenance windows
description: Manage maintenance windows for clusters in Yugabyte Cloud.
headcontent:
image: /images/section_icons/manage/backup.png
menu:
  latest:
    identifier: cloud-maintenance
    parent: cloud-clusters
    weight: 400
isTocNested: true
showAsideToc: true
---

Yugabyte occasionally performs maintenance on clusters. This can include infrastructure and database upgrades. Depending on the type of maintenance, your cluster may be restarted, which disrupts service briefly. Yugabyte notifies you in advance of any upcoming maintenance via email. One week before a scheduled maintenance, an **Upcoming Maintenance** badge is displayed on the cluster.

Yugabyte only performs cluster maintenance, including database upgrades, during scheduled maintenance windows. The maintenance window is a weekly four hour interval during which Yugabyte may perform maintenance on the cluster.

You can manage when maintenance is done on standard clusters (these features are not available for free clusters) in the following ways:

- [Set the maintenance window schedule](#set-the-cluster-maintenance-window-schedule).
- [Schedule exclusion periods](#set-a-maintenance-exclusion-period), during which Yugabyte won't perform maintenance; any scheduled maintenance is delayed until the next maintenance window after the exclusion period.
- Delay a scheduled maintenance to the next available window. You can't delay a scheduled maintenance more than 7 days in advance.

You set the cluster maintenance window, exclusion periods, and review upcoming maintenance events using the cluster **Maintenance** tab.

![Cloud Cluster Maintenance page](/images/yb-cloud/cloud-clusters-maintenance.png)

To view details of upcoming scheduled maintenance, click the **Upcoming Maintenance** badge, or select the maintenance in the **Scheduled Maintenance** list to display the **Maintenance Details**.

To delay a scheduled maintenance, click **Delay to next available window** on the **Maintenance Details** sheet.

If the scheduled maintenance is a database upgrade, you can start the upgrade by clicking **Upgrade Now** on the **Maintenance Details** sheet.

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
- No high availability - During maintenance, one node is always offline. If one of the remaining 2 nodes goes down in a 3 node cluster, you lose access to the database. For clusters with more nodes, there is less risk.
- Less bandwidth - During maintenance, traffic is diverted to the running nodes. To mitigate this, set your maintenance window to a low traffic period. You can also add nodes (scale out) prior to the upgrade.
