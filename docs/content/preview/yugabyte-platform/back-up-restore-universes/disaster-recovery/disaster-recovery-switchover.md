---
title: Planned Switchover with disaster recovery
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using disaster recovery
headContent: Switch application traffic to the DR replica
menu:
  preview_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-switchover
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the DR primary to the DR replica without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

Switchover can be used by enterprises when performing regular business continuity or disaster recovery testing. Switchover can also used for failback purposes. That is, after a [Failover followed by a Repair DR](../disaster-recovery-failover/), you can use switchover to switch traffic back to the original DR primary after it is brought back online.

## Perform switchover

First, confirm there is no excessive lag between the DR primary and replica. You can [monitor lag](../disaster-recovery-setup/#monitor-replication) on the **xCluster Disaster Recovery** tab.

If the DR config has any tables that don't have a replication status of Operational, switchover will be unsuccessful. In that case, you can do one of the following:

- Perform a full copy from the DR primary to the DR replica.
- [Unplanned Failover](../disaster-recovery-failover/).

Verify that the list of tables in the DR primary's database(s) match the list of tables in the DR replica's database(s). Switchover will fail if there is a mismatch in this list.

Use the following steps to perform a planned switchover:

1. Ensure there is no significant lag between DR primary and replica, and that there are no critical alerts active on either universe.

1. Stop the application traffic on the DR primary.

1. Navigate to your DR primary universe and select **xCluster Disaster Recovery**.

1. Click **Actions** and choose **Switchover**.

1. Enter the name of the DR replica and click **Initiate Switchover**.

    The switchover process waits for all remaining changes on the current DR primary to be replicated to the DR replica.

1. Resume the application traffic on the new DR primary.
