---
title: Planned Switchover with transactional xCluster replication
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using transactional xCluster replication between universes
headContent: Switch application traffic to the standby universe without data loss
menu:
  v2.25:
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

{{< warning title="Automatic mode requirements" >}}

If you are performing a switchover in automatic mode, you must not run any DDLs while the switchover is being done.  Stop submitting any DDLs and wait for any previously submitted DLLs to be replicated before proceeding.  Checking xCluster safe time (see [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/)) will tell you the latest time up to which DDLs have been replicated. See {{<issue 26028>}}.

{{< /warning >}}

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

If you are using CDC to move data out of YugabyteDB, while no new writes are occurring, you must also wait for the CDC target to catch up. Check that the lag is zero (or close to zero), and verify the event count metrics of the connector being streamed are 0.

### Fix up sequences and serial columns

{{< note >}}
Skip this step if you are using xCluster replication automatic mode.
{{< /note >}}

xCluster only replicates sequence data in automatic mode.  If you are not using automatic mode, you need to manually synchronize the sequence next values on universe B after switchover to match those on universe A. This ensures that new writes on universe B do not conflict with existing data.

For example, if you have a SERIAL column in a table and the highest value in that column after switchover is 500, you need to set the sequence associated with that column to a value higher than 500, such as 501. This ensures that new writes on universe B do not conflict with existing data.

Use the [nextval](../../../../api/ysql/exprs/sequence_functions/func_nextval/) function to set the sequence next values appropriately.

### Delete the old replication group

Run the following command against A to delete the old replication group.

{{% readfile "includes/transactional-drop.md" %}}

In addition, if you are using CDC to move data out of YugabyteDB, delete CDC on A (that is, de-configure CDC by deleting publications and slots), and start up CDC on B (that is, create publications and slots on B).

### Switch applications to the new Primary universe (B)

The old Standby (B) is now the new Primary universe, and the old Primary (A) is the new Standby universe. Update the application connection strings to point to the new Primary universe (B).

If you are using CDC to move data out of YugabyteDB, point your CDC sink to pull from B (the newly promoted database) by starting the connector in `snapshot.mode=never`.
