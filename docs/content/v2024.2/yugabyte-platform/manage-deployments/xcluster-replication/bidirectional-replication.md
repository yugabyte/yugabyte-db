---
title: Bidirectional replication using xCluster
headerTitle: Bidirectional replication
linkTitle: Bidirectional replication
description: Bidirectional replication using xCluster
headContent: Replicate data in both directions
menu:
  v2024.2_yugabyte-platform:
    parent: xcluster-replication
    identifier: bidirectional-replication
    weight: 50
type: docs
---

With bidirectional replication, both universes can perform reads and writes, and writes are replicated in both directions. This is also referred to as [Active-active multi-master](../../../../architecture/docdb-replication/async-replication/#active-active-multi-master).

You set up bidirectional replication using YugabyteDB Anywhere by creating two separate non-transactional xCluster Replication configurations. Under this scenario, you create a replication configuration from universe A to universe B, and then you create a new replication configuration from universe B to universe A.

![Active-Active Multi-Master](/images/architecture/replication/active-active-deployment-new.png)

YugabyteDB Anywhere detects an xCluster setup is for bidirectional if the tables that are being added to the replication are already part of a replication in the reverse direction (from A to B). In this scenario, YugabyteDB Anywhere skips the full copy for tables on universe B that are already in replication.

{{<note title="Bidirectional xCluster deployment">}}
Due to its operational complexity, bidirectional xCluster is not recommended for most use cases. If you are considering a bidirectional xCluster deployment, contact {{% support-general %}}.
{{</note>}}

## Limitations

In addition to the regular [limitations](../#limitations), Bidirectional replication can only be done using [non-transactional replication](../#xcluster-configurations).

## Prerequisites

Create two universes as described in [Set up xCluster Replication Prerequisites](../xcluster-replication-setup/#prerequisites).

## Set up bidirectional replication

To set up bidirectional replication from universe A to universe B, and vice versa on the same set of tables or databases, do the following:

1. Make sure that there are no writes happening on universe B.
1. Set up xCluster Replication from A to B following the steps in [Deploy xCluster](../../../../deploy/multi-dc/async-replication/async-deployment/). A full copy of the selected tables or databases may be performed as part of this operation.
1. Set up xCluster Replication from universe B to A following the steps in [Deploy xCluster](../../../../deploy/multi-dc/async-replication/async-deployment/).

    No full copy is performed for the reverse replication. It is important to have no writes on B during this setup, as these writes might not be replicated to universe A.

For information on how to monitor and manage the replication configurations, refer to [Monitoring and alerts](../xcluster-replication-setup/#monitoring-and-alerts).

### Add a database to an existing bidirectional replication

To add a newly created database to bidirectional replication, follow the instructions in [Add a database to an existing replication](../xcluster-replication-setup/#add-a-database-to-an-existing-replication) in both directions.

For best results, add databases to bidirectional configurations right after you create them, and before any writes are performed on the tables in them.

You can add databases that have data to replication in both directions, but note that YugabyteDB Anywhere performs a [full copy](../xcluster-replication-setup/#full-copy-during-xcluster-setup) from the source to the target, and this causes the existing database on the target to be re-created.

## Restart bidirectional replication

In case of an extended network partition, or if replication is broken for any other reason, you may need to restart replication.

Restarting one side of an existing bidirectional replication configuration is disabled to prevent potential data inconsistencies. To restart, you must use the following steps.

Before restarting bidirectional replication, you need to identify the universe whose data is more up to date.

To restart a bidirectional replication setup:

1. Identify the universe whose data is more up to date.
1. Delete the replication configuration where this universe is the target universe.
1. Restart the other replication configuration, where this universe is the source universe.
1. Set up the replication configuration deleted in step (2) again, with this universe as the target universe.

## DDL operations in bidirectional replication

In general, for DDL operations, you can follow the steps in [Manage tables and indexes](../xcluster-replication-ddl/) while taking care to update the xCluster Replication configuration on both configurations involved.

However, in some special cases, you need to follow different procedures.

### Add table or partition

It is highly recommended that you add tables to bidirectional configurations immediately after creating them and before any writes are performed.

To add a table to bidirectional replication:

1. Add the table to both universes.
1. Add the table to both replication configurations by following the steps in [Add a table to xCluster Replication](../xcluster-replication-ddl/#add-a-table-to-replication).

Non-empty YSQL tables can be added to replication in both directions, but this is not advisable because YSQL is at database granularity, and no full copy is performed. Any initial writes to the table are not replicated, and data might be inconsistent between the source and target universes. To fix any inconsistencies, follow the steps for [Restart bidirectional replication](#restart-bidirectional-replication).

For YCQL non-index tables, adding non-empty tables to replication works as expected, with no additional steps required.

For index tables, follow the steps in [Add index table](#add-index-table).

### Add index table

New YSQL indexes are automatically added to xCluster replication if the YSQL table being indexed is bidirectionally replicated. Adding new indexes is supported even if the table being indexed contains data and is actively receiving writes on both the universes.

To add an index table to a bidirectional replication, you must create the index table on both universes _at the same time_.
