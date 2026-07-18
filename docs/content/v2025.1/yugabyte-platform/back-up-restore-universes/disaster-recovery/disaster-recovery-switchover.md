---
title: Planned Switchover with disaster recovery
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using disaster recovery
headContent: Switch application traffic to the DR replica
menu:
  v2025.1_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-switchover
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the DR primary to the DR replica without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

Switchover can be used by enterprises when performing regular business continuity or disaster recovery testing. Switchover can also used for failback purposes. That is, after a [Failover followed by a Repair DR](../disaster-recovery-failover/), you can use switchover to switch traffic back to the original DR primary after it is brought back online.

## Perform switchover

First, confirm there is no excessive lag between the DR primary and replica. You can [monitor lag](../disaster-recovery-setup/#monitor-replication) on the **xCluster Disaster Recovery** tab.

While the switchover task is in progress, both universes are in read-only mode and reject write operations.

If the DR configuration has any tables that don't have a replication status of Operational, switchover will be unsuccessful. In that case, you can do one of the following:

- Perform a full copy from the DR primary to the DR replica.
- [Unplanned Failover](../disaster-recovery-failover/).

Verify that the list of tables in the DR primary's database(s) match the list of tables in the DR replica's database(s). Switchover will fail if there is a mismatch in this list.

Use the following steps to perform a planned switchover:

1. Ensure there is no significant lag between DR primary and replica, and that there are no critical alerts active on either universe.

1. Stop the application traffic on the DR primary.

1. Navigate to your DR primary universe **xCluster Disaster Recovery** tab and select the replication configuration.

1. Click **Actions** and choose **Switchover**.

1. Enter the name of the DR replica and click **Initiate Switchover**.

    The switchover process waits for all remaining changes on the current DR primary to be replicated to the DR replica.

1. Resume the application traffic on the new DR primary.

### Fix up sequences and serial columns

In v2025.1.1 or earlier, if you are using sequences, you need to manually synchronize the sequence next values on the new DR primary after failover. This ensures that new writes on the new DR primary do not conflict with existing data.

For example, if you have a SERIAL column in a table and the highest value in that column after switchover is 500, you need to set the sequence associated with that column to a value higher than 500, such as 501. This ensures that new writes on new DR primary do not conflict with existing data.

Use the [nextval](../../../../api/ysql/exprs/sequence_functions/func_nextval/) function to set the sequence next values appropriately.

## Abort, retry, and rollback

While in progress, the switchover task is displayed on the universe **Tasks** tab. From there you can abort, retry, and roll back the switchover task.

During switchover, writes to databases undergoing switchover are blocked on both universes until the task completes. This process typically takes only a few seconds. The Abort, Retry, and Rollback options provide flexibility in case the switchover is taking too long, and you want to quickly restore write availability on at least one universe.

If a switchover task fails or you abort it, you have the option to roll back to the previous state, keeping the current primary universe as primary and the replica universe as replica.

Rollback is only possible if the switchover hasn't progressed beyond a certain point. If you make a rollback request beyond this point, the system will return an error indicating that rollback is no longer possible. At that stage, the new primary universe will already be able to accept writes.

To revert roles, you must retry the original switchover to success, and then (if needed) initiate a new switchover task to swap roles again.
