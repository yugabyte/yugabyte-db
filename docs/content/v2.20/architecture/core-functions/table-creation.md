---
title: Table creation
headerTitle: Table creation
linkTitle: Table creation
description: Learn how YugabyteDB creates tables and manages schema changes.
menu:
  v2.20:
    identifier: table-creation
    parent: core-functions
    weight: 1184
type: docs
---

In YugabyteDB, the table creation initiated by the user is handled by the YB-Master leader using an asynchronous API. The YB-Master leader returns a success for the API call once it has replicated both the table schema and all the other information needed to perform the table creation to the other YB-Masters in the Raft group to make it resilient to failures.

To create a table, the YB-Master leader performs a number of steps.

## Validation

The YB-Master leader validates the table schema and creates the desired number of tablets for the table. The tablets are not yet assigned to YB-TServers.

## Replication

The YB-Master leader replicates the table schema and the newly created (and still unassigned) tablets to the YB-Master Raft group. This ensures that the table creation can succeed even if the current YB-Master
leader fails.

## Acknowledgement

At this point, the asynchronous table creation API returns a success, since the operation can proceed even if the current YB-Master leader fails.

## Execution

The YB-Master leader assigns each of the tablets to as many YB-TServers as the replication factor of the table. The tablet peer placement is done in such a manner as to ensure that the desired fault tolerance is
achieved and the YB-TServers are evenly balanced with respect to the number of tablets they are assigned. In some deployment scenarios, the assignment of the tablets to YB-TServers may need to satisfy additional constraints such as distributing the individual replicas of each tablet across multiple cloud providers, regions, and availability zones.

## Continuous monitoring

The YB-Master leader monitors the entire tablet assignment operation and reports on its progress and completion to the user-issued API calls.

## Examples

Suppose a table is created in a YugabyteDB universe with four nodes. In addition, suppose that the table has sixteen tablets and a replication factor of 3. The YB-Master leader validates the schema, creates sixteen tablets (forty-eight tablet peers in total, due to the replication factor of 3) and replicates the data needed for table creation across a majority of YB-Masters using Raft. The following diagram depicts the start of the table creation process: 

![create_table_masters](/images/architecture/create_table_masters.png)

The following diagram depicts the process of the newly-created tablets being assigned to a number of YB-TServers:

![tserver_tablet_assignment](/images/architecture/tserver_tablet_assignment.png)

The tablet-peers hosted on different YB-TServers form a Raft group and elect a leader. For all reads and writes of keys belonging to this tablet, the tablet-peer leader and the Raft group are respectively responsible. Once assigned, the tablets are owned by the YB-TServers until the ownership is changed by the YB-Master either due to an extended failure or a future load-balancing event, as demonstrated by the following diagram:

![tablet_peer_raft_groups](/images/architecture/tablet_peer_raft_groups.png)

If one of the YB-TServers hosting the tablet leader fails, the tablet Raft group immediately re-elects a leader for purposes of handling I/O. Therefore, the YB-Master is not in the critical I/O path. If a YB-TServer remains in a failed state for an extended period of time, the YB-Master finds a set of suitable candidates to which to rereplicate its data. It does so in a throttled and graceful manner.
