---
title: Bidirectional replication using xCluster
headerTitle: Bidirectional replication
linkTitle: Bidirectional replication
description: Bidirectional replication using xCluster
headContent: Switch application traffic to the DR replica
menu:
  preview_yugabyte-platform:
    parent: xcluster-replication
    identifier: bidirectional-replication
    weight: 50
type: docs
---

You can set up bidirectional replication using either the YugabyteDB Anywhere UI or API by creating two separate non-transactional replication configurations. Under this scenario, you create a replication configuration from universe A to universe B, and then you create a new replication configuration from universe B to universe A.

YugabyteDB Anywhere detects an xCluster setup is for bidirectional if the tables that are being added to the replication are already part of a replication in the reverse direction (from A to B). In this scenario, YugabyteDB Anywhere skips the full copy for tables on universe B that are already in replication.

## Prerequisites

- Bidirectional replication can be done only using non-transactional replications. Transactional replication is not supported because transactional replication puts the target universe in read-only mode. For more information, see [Asynchronous replication modes](../../../../architecture/docdb-replication/async-replication/#asynchronous-replication-modes).
- The same pre-requisite as the unidirectional non-transactional replication, mentioned at [put a link here]
- You can't use the YugabyteDB Anywhere UI to create two separate replication configurations for YSQL, each containing a subset of the database tables.
- Active-active bidirectional replication is not supported because the backup or restore would wipe out the existing data. This means that copying data can be done only if an xCluster configuration with reverse direction for a table does not exist. It is recommended to set up replication from your active universe to the passive target, and then set up replication for the target to the source universe. To restart a replication with a full copy, the reverse replication must be deleted.

## Set up replication

To set up bidirectional replication from A -> B and B -> A on the same set of tables/dbs, do the following:

1. Make sure that there are no writes happening on cluster B.
1. Set up xCluster replication from A -> B following the steps in [regular Setup Replication docs]. A full copy of the selected tables/dbs might be performed as part of this operation.
1. Set up xCluster replication from B -> A following the steps in [regular setup replication docs].

    No full copy is performed, so it is important to have no writes on B during this setup as these writes might not be replicated to universe A.

To monitor and manage the replication configurations, please refer to [put a link here to the main doc]

## Restart replication

Restarting a bidirectional replication A <-> B can cause unreplicated data on one of the universes to be lost because a full copy is performed as part of the restart. It is important to choose the order of steps below to identify the universe whose data is more up to date.

To restart a bidirectional replication setup:

1. Identify the universe whose data is more up to date.
1. Delete the replication configuration where this universe is the target universe.
1. Restart the other replication configuration, where this universe is the source universe.
1. Set up the replication configuration deleted in step (2) again, with this universe as the target universe.

Note that restarting one side of an existing bidirectional replication configuration without performing the step (2) as above simply re-establishes the replication configuration from the current data without performing a full copy. This can cause inconsistencies in the data between the source and target universes.

## DDL operations in bidirectional replication

In general, for DDL operations, you can follow the steps in [link to DDL operations in the unidirectional replication] while taking care to perform the YBA operation on both the replication configurations involved. However, there are a few special cases that are called out below for table/partition addition and index table addition.

### Add table or partition

It is highly recommended that the tables should be added to bidirectional configurations right after they are created before any writes are performed on these tables.

To add a newly created table to the bidirectional replication:

1. CREATE TABLE on both sides.
1. Add the table to both replication configurations in YBA following the steps at [Add table to xcluster replication].

Non-empty tables can still be added to replication in both directions as above but this is not advisable. In this case, no full copy is performed so any initial writes to the table are not replicated and data might be inconsistent between the source and target universes. To fix any such inconsistencies, you can use the workflow in [Restart replication].

### Add index table

To add an index table to a bidirectional replication follow the following steps:

1. Stop workload on its main table.
1. Wait for the replication lag for the corresponding main table to become 0.
1. Create the index table on both universes.
1. Go to YBA UI and add the newly created index tables to both replication configurations using the mentioned steps above in [Add table to xcluster replication].

Note: If the main table has data, and the index table is added to the replication configs without stopping the workload against the main table, the index table can be potentially inconsistent between the two universes.
