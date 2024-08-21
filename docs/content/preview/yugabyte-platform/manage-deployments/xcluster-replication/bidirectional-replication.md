---
title: Bidirectional replication using xCluster
headerTitle: Bidirectional replication
linkTitle: Bidirectional replication
description: Bidirectional replication using xCluster
headContent: Replicate data in both directions
menu:
  preview_yugabyte-platform:
    parent: xcluster-replication
    identifier: bidirectional-replication
    weight: 50
type: docs
---

With bidirectional replication, both universes can perform reads and writes, and writes are replicated in both directions. This is also referred to as [Active-active multi-master](../../../../architecture/docdb-replication/async-replication/#active-active-multi-master).

You set up bidirectional replication using YugabyteDB Anywhere by creating two separate non-transactional xCluster replication configurations. Under this scenario, you create a replication configuration from universe A to universe B, and then you create a new replication configuration from universe B to universe A.

![Active-Active Multi-Master](/images/architecture/replication/active-active-deployment-new.png)

YugabyteDB Anywhere detects an xCluster setup is for bidirectional if the tables that are being added to the replication are already part of a replication in the reverse direction (from A to B). In this scenario, YugabyteDB Anywhere skips the full copy for tables on universe B that are already in replication.

{{<note title="Bidirectional xCluster deployment">}}
Due to its operational complexity, bidirectional xCluster is not recommended for most use cases. If you are considering a bidirectional xCluster deployment, contact {{% support-general %}}.
{{</note>}}

## Limitations

In addition to the regular [limitations](../#limitations), Bidirectional replication can only be done using [non-transactional replication](../#xcluster-configurations).

## Prerequisites

Create two universes as described in [Prerequisites](../xcluster-replication-setup/#prerequisites).

## Set up bidirectional replication

To set up bidirectional replication from universe A to universe B, and vice versa on the same set of tables or databases, do the following:

1. Make sure that there are no writes happening on universe B.
1. Set up xCluster replication from A to B following the steps in [Deploy xCluster](../../../../deploy/multi-dc/async-replication/async-deployment/). A full copy of the selected tables or databases may be performed as part of this operation.
1. Set up xCluster replication from universe B to A following the steps in [Deploy xCluster](../../../../deploy/multi-dc/async-replication/async-deployment/).

    No full copy is performed for the reverse replication. It is important to have no writes on B during this setup, as these writes might not be replicated to universe A.

For information on how to monitor and manage the replication configurations, refer to [Monitoring and alerts](../xcluster-replication-setup/#monitoring-and-alerts).

### Add a database to an existing bidirectional replication

To add a newly created database to bidirectional replication, follow the instructions in [Add a database to an existing replication](../xcluster-replication-setup/#add-a-database-to-an-existing-replication) in both directions.

For best results, add databases to bidirectional configurations right after you create them, and before any writes are performed on the tables in them.

You can add databases that have data to replication in both directions, but note that YugabyteDB Anywhere performs a [full copy](../xcluster-replication-setup/#full-copy-during-xcluster-setup) from the source to the target, and this causes the existing database on the target to be re-created.

## Restart bidirectional replication

In case of an extended network partition, or if replication is broken for any other reason, you may need to restart replication.

Before restarting bidirectional replication, you need to identify the universe whose data is more up to date.

To restart a bidirectional replication setup:

1. Identify the universe whose data is more up to date.
1. Delete the replication configuration where this universe is the target universe.
1. Restart the other replication configuration, where this universe is the source universe.
1. Set up the replication configuration deleted in step (2) again, with this universe as the target universe.

Note that restarting one side of an existing bidirectional replication configuration without performing step 2 simply re-establishes the replication configuration from the current data without performing a full copy. This can cause inconsistencies in the data between the source and target universes.

## DDL operations in bidirectional replication

In general, for DDL operations, you can follow the steps in [Manage tables and indexes](../xcluster-replication-ddl/) while taking care to update the xCluster replication configuration on both configurations involved.

However, in some special cases, you need to follow different procedures.

### Add table or partition

It is highly recommended that you add tables to bidirectional configurations immediately after creating them and before any writes are performed.

To add a table to bidirectional replication:

1. Add the table to both universes.
1. Add the table to both replication configurations by following the steps in [Add a table to xCluster replication](../xcluster-replication-ddl/#add-a-table-to-replication).

Non-empty tables can still be added to replication in both directions as above, but this is not advisable. In this case, no full copy is performed so any initial writes to the table are not replicated and data might be inconsistent between the source and target universes. To fix any inconsistencies, [restart the replication](#restart-replication).

### Add index table

To add an index table to a bidirectional replication, do the following:

1. Stop the workload on the main table.
1. Wait for the replication lag for the corresponding main table to become 0.
1. Create the index table on both universes.
1. Add the newly created index tables to both replication configurations by following the steps in [Add a table to xCluster replication](../xcluster-replication-ddl/#add-a-table-to-replication).

Note: If the main table has data, and the index table is added to the replication configurations without stopping the workload against the main table, the index table can be potentially inconsistent between the two universes.
