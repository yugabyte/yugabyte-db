---
title: Unplanned failover to a target Aeon cluster
headerTitle: Unplanned failover
linkTitle: Failover
description: Unplanned failover to a target cluster
headContent: Failover of application traffic to the DR target
menu:
  stable_yugabyte-cloud:
    parent: disaster-recovery-aeon
    identifier: disaster-recovery-failover-aeon
    weight: 30
type: docs
---

Unplanned failover is the process of switching application traffic to the Target cluster in case the Source cluster becomes unavailable. One of the common reasons for such a scenario is an outage of the primary region.

## Perform failover

Use the following procedure to perform an unplanned failover to the Target and resume applications.

If the Source is terminated for some reason, do the following:

1. Stop the application traffic to ensure no more updates are attempted.

1. Navigate to your Source cluster **Disaster Recovery** tab.

1. Note the **Potential data loss on failover** to understand the extent of possible data loss as a result of the outage, and determine if the extent of data loss is acceptable for your situation.

    - The potential data loss is computed as the safe time lag that existed at the current safe time on the Target. Because only a portion of the data changes can be replicated to the target universe, any partially replicated data will be discarded to ensure a consistent snapshot at the latest safe point in time. The potential data loss on failover indicates the volume of such data that will be dropped.
    - Use the **Tables** tab to understand which specific tables have the highest safe time lag and replication lag.

    For more information on replication metrics, refer to [Replication](../../../../launch-and-manage/monitor-and-alert/metrics/replication/).

1. To proceed, click **Switchover** and choose **Failover**.

1. Enter the name of the Target and click **Failover**.

    The Target becomes the new Source.

1. Resume the application traffic on the new Source.

At this point, the DR configuration is halted.

![Disaster recovery halted](/images/yb-cloud/managed-dr-halted.png)

To resume DR (with the old Source becoming the new Target), click **Restart Replication**. There may be some delay before the option becomes available while the cluster comes back online.

After restarting replication, if you want to make the new Target (previously the Source) act as the source again, you can [perform a switchover](../disaster-recovery-switchover/).
