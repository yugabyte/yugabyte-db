---
title: Unplanned failover with disaster recovery
headerTitle: Unplanned failover
linkTitle: Failover
description: Unplanned failover using disaster recovery
headContent: Failover of application traffic to the DR replica
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-failover
    weight: 30
type: docs
---

Unplanned failover is the process of switching application traffic to the DR replica universe in case the DR primary universe becomes unavailable. One of the common reasons for such a scenario is an outage of the primary region.

## Perform failover

Use the following procedure to perform an unplanned failover to the DR replica and resume applications.

If the DR primary is terminated for some reason, do the following:

1. Stop the application traffic to ensure no more updates are attempted.

1. Navigate to your DR primary universe and select **xCluster Disaster Recovery**.

1. Note the **Potential data loss on failover** to understand the extent of possible data loss as a result of the outage, and determine if the extent of data loss is acceptable for your situation.

    - The potential data loss is computed as the safe time lag that existed at the current safe time on the DR replica.
    - Use the **Tables** tab to understand which specific tables have the highest safe time lag and replication lag.

    For more information on replication metrics, refer to [Replication](../../../../launch-and-manage/monitor-and-alert/metrics/replication/).

1. To proceed, click **Actions** and choose **Failover**.

1. Enter the name of the DR replica and click **Initiate Failover**.

    YugabyteDB Anywhere switches the DR replica universe to be the DR primary.

1. Resume the application traffic on the new DR primary.

At this point, the DR configuration is halted and needs to be repaired.

## Repair DR after failover

There are two options to repair a DR that has failed over:

- If the original DR primary has recovered and is fully functional with no active alerts, you can configure DR to use this universe as a DR replica.
- If the original DR primary cannot be recovered, create a new universe to be configured to act as the DR replica (see [Prerequisites](../disaster-recovery-setup/#prerequisites)).

In both cases, repairing DR involves making a full copy of the databases through the backup-restore process.

To repair DR, do the following:

1. Navigate to your (new) DR primary universe and select **xCluster Disaster Recovery**.

1. Click **Repair DR**.

1. If the original DR primary has recovered and is fully functional with no active alerts, choose **Reuse the current DR replica**.

    To use a new universe as the DR replica, choose **Select a new universe as DR replica** and select the universe.

1. Click **Initiate Repair**.

If your eventual desired configuration is for the other universe (that is, the one you have added to DR to act as DR replica) to be the DR primary, follow the steps for [Planned switchover](../disaster-recovery-switchover/).
