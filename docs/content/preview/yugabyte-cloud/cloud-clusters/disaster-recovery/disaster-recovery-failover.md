---
title: Unplanned failover to a target Aeon cluster
headerTitle: Unplanned failover
linkTitle: Failover
description: Unplanned failover to a target cluster
headContent: Failover of application traffic to the DR target
menu:
  preview_yugabyte-cloud:
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

1. Navigate to your Source cluster **Disaster Recovery** tab and select the replication configuration.

1. Note the **Potential data loss on failover** to understand the extent of possible data loss as a result of the outage, and determine if the extent of data loss is acceptable for your situation.

    - The potential data loss is computed as the safe time lag that existed at the current safe time on the Target.
    - Use the **Tables** tab to understand which specific tables have the highest safe time lag and replication lag.

    For more information on replication metrics, refer to [Replication](../../../../launch-and-manage/monitor-and-alert/metrics/replication/).

1. To proceed, click **Switchover** and choose **Failover**.

1. Enter the name of the Target and click **Failover**.

1. Click **Restart Replication**.

1. Resume the application traffic on the new Source.

At this point, the DR configuration is halted and needs to be repaired.

![Disaster recovery failed](/images/yb-platform/disaster-recovery/disaster-recovery-failed.png)

## Repair DR after failover

There are two options to repair a DR that has failed over:

- If the original Source has recovered and is fully functional with no active alerts, you can configure DR to use this cluster as a Target.
- If the original Source cannot be recovered, create a new cluster to be configured to act as the Target (see [Prerequisites](../disaster-recovery-setup/#prerequisites)).

In both cases, repairing DR involves making a full copy of the databases through the backup-restore process.

To repair DR, do the following:

1. Navigate to your (new) Source cluster **Disaster Recovery** tab and select the replication configuration.

1. Click **Repair DR** to display the **Repair DR** dialog.

    ![Repair DR](/images/yb-platform/disaster-recovery/disaster-recovery-repair.png)

1. If the current Target (formerly the Source) has recovered and is fully functional with no active alerts, choose **Reuse the current Target**.

    To use a new cluster as the Target, choose **Select a new cluster as Target** and select the cluster.

1. Click **Initiate Repair**.

After the repair is complete, if your eventual desired configuration is for the Target (that is, the former Source if you chose Reuse, or the new one you added to DR to act as Target) to be the Source, follow the steps for [Planned switchover](../disaster-recovery-switchover/).

{{< warning title="Important" >}}
Do not attempt a switchover if you have not first repaired DR.
{{< /warning >}}
