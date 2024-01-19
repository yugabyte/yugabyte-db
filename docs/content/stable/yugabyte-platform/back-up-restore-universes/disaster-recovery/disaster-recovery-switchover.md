---
title: Planned Switchover with disaster recovery
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using disaster recovery
headContent: Switch application traffic to the DR replica
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-switchover
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the DR primary to the DR replica without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

Switchover can be used by enterprises when performing regular business continuity or disaster recovery testing. Switchover is also used for failback after a [Failover](../disaster-recovery-failover/); that is, switching traffic back to the former DR primary after it is brought back online.

## Perform switchover

First, confirm there is no lag between the DR primary and replica. You can monitor lag on the **Disaster Recovery** tab.

If lag exceeds `log_min_seconds_to_retain` seconds, and WALs have been garbage collected, switchover will be unsuccessful. In that case, you can do one of the following:

- Perform a full copy from the DR primary to the DR replica.
- [Unplanned Failover](../disaster-recovery-failover/).

Use the following steps to perform a planned switchover:

1. Ensure there is no significant lag between DR primary and replica and that there are no critical alerts active on either universe.

1. Stop the application traffic on the DR primary.

1. Navigate to your primary universe and select **Disaster Recovery**.

1. Click **Actions** and choose **Switchover**.

1. Enter the name of the DR replica and click **Initiate Switchover**.

    The switchover process waits for all remaining changes on the current DR primary to be replicated to the DR replica.

1. Resume the application traffic on the new DR primary.
