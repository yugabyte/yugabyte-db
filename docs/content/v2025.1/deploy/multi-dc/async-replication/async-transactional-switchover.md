---
title: Planned Switchover with transactional xCluster replication
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using transactional xCluster replication between universes
headContent: Switch application traffic to the standby universe without data loss
menu:
  v2025.1:
    parent: async-replication-transactional
    identifier: async-transactional-switchover
    weight: 40
tags:
  other: ysql
type: docs
---

Planned switchover is the process of switching write access from the Primary universe to the Standby universe without losing any data (zero RPO). This operation primarily involves updating metadata and typically completes within 30 seconds. However, because application traffic needs to be redirected to the new universe, it is advisable to perform this operation during a maintenance window.

## Perform switchover

Assuming universe A is the Primary and universe B is the Standby, use the following steps to perform a planned switchover to change the replication direction from A->B to B->A.

### Precheck

- Check existing replication group health

  To minimize disruptions and the duration of the switchover, ensure that the existing replication is healthy and the lag is under 5 seconds. For information, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).

- Check prerequisites

  Ensure that all prerequisites for setting up xCluster are satisfied for the new replication direction. For more information, refer to the [xCluster prerequisites](../#prerequisites).

### Set up replication in the reverse direction

Set up xCluster Replication from the Standby universe (B) to Primary universe (A) by following the steps in [Set up transactional xCluster](../../async-replication/async-transactional-setup-automatic/).

Skip the bootstrap (backup/restore) step, as the data is already present in both universes.

Ensure that the mode of replication used matches the original setup. Continuously monitor the health of the new replication to prevent any unexpected issues post-switchover.

This step puts both universes in read-only mode, allowing universe B to catch up with universe A.

### Wait for replication to catch up

Wait for any pending updates to propagate to B:

1. Get the current time on A.

1. Wait until the [xCluster safe time](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/#xcluster-safe-time) on B is a few seconds greater than the time obtained in the previous step.

{{< note title="Note" >}}
The lag and skew values might be non-zero as they are estimates based on the last time the tablets were polled. Because no new writes can occur during this period, you can be certain that no data is lost within this timeframe.
{{< /note >}}

### Fix up sequences and serial columns

{{< note >}}
_Not applicable for Automatic mode_
{{< /note >}}

Because xCluster does not replicate sequence data, you need to manually synchronize the sequence next values on universe B to match those on universe A. This ensures that new writes on universe B do not conflict with existing data.

Use the [nextval](../../../../api/ysql/exprs/sequence_functions/func_nextval/) function to set the sequence next values appropriately.

### Delete the old replication group

Run the following command against A to delete the old replication group.

{{% readfile "includes/transactional-drop.md" %}}

### Switch applications to the new Primary universe (B)

The old Standby (B) is now the new Primary universe, and the old Primary (A) is the new Standby universe. Update the application connection strings to point to the new Primary universe (B).
