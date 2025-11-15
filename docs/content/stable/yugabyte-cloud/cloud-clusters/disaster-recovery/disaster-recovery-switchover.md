---
title: Planned Switchover to a target Aeon cluster
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using disaster recovery
headContent: Switch application traffic to the DR target
menu:
  stable_yugabyte-cloud:
    parent: disaster-recovery-aeon
    identifier: disaster-recovery-switchover-aeon
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the Source to the Target without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

Switchover can be used by enterprises when performing regular business continuity or disaster recovery testing. Switchover can also used for failback purposes. That is, after a [Failover followed by a Repair DR](../disaster-recovery-failover/), you can use switchover to switch traffic back to the original Source after it is brought back online.

## Perform switchover

First, confirm there is no excessive lag between the Source and Target. You can [monitor lag](../disaster-recovery-setup/#monitor-replication) on the **Disaster Recovery** tab.

While the switchover task is in progress, both clusters are in read-only mode and reject write operations.

If the DR configuration has any tables that don't have a replication status of Operational, switchover will be unsuccessful. In that case, you can do one of the following:

- On the **Database and Tables** tab, check the replication status of the tables. For any tables in a bad state, take corresponding action to fix the problem, and then run switchover. In severe cases, where you cannot fix the replication status, (such as Missing Op ID), you will need to perform a full copy from the Source to the Target.
- [Unplanned Failover](../disaster-recovery-failover/).

Verify that the list of tables in the Source's database(s) match the list of tables in the Target's database(s). Switchover will fail if there is a mismatch in this list.

Use the following steps to perform a planned switchover:

1. Ensure there is no significant lag between Source and Target, and that there are no critical alerts active on either cluster.

1. Stop the application traffic on the Source.

1. Navigate to your Source cluster **Disaster Recovery** tab.

1. Click **Switchover** and choose **Switchover**.

1. Enter the name of the Target and click **Switchover**.

    The switchover process waits for all remaining changes on the current Source to be replicated to the Target.

1. Resume the application traffic on the new Source.
