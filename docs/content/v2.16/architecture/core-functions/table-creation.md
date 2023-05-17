---
title: Table creation
headerTitle: Table creation
linkTitle: Table creation
description: Learn how YugabyteDB creates tables and manages schema changes.
menu:
  v2.16:
    identifier: table-creation
    parent: core-functions
    weight: 1184
type: docs
---

In YugabyteDB, user-issued table creation is handled by the YB-Master leader, and is an asynchronous API. The
YB-Master leader returns a success for the API once it has replicated both the table schema as well
as all the other information needed to perform the table creation to the other YB-Masters in the
RAFT group to make it resilient to failures.

The table creation is accomplished by the YB-Master leader using the following steps:

## Step 1. Validation

Validates the table schema and creates the desired number of tablets for the table. These tablets
are not yet assigned to YB-TServers.

## Step 2. Replication

Replicates the table schema as well as the newly created (and still unassigned) tablets to the
YB-Master RAFT group. This ensures that the table creation can succeed even if the current YB-Master
leader fails.

## Step 3. Acknowledgement

At this point, the asynchronous table creation API returns a success, since the operation can
proceed even if the current YB-Master leader fails.

## Step 4. Execution

Assigns each of the tablets to as many YB-TServers as the replication factor of the table. The
tablet-peer placement is done in such a manner as to ensure that the desired fault tolerance is
achieved and the YB-TServers are evenly balanced with respect to the number of tablets they are
assigned. Note that in certain deployment scenarios, the assignment of the tablets to YB-TServers
may need to satisfy many more constraints such as distributing the individual replicas of each
tablet across multiple cloud providers, regions and availability zones.

## Step 5. Continuous monitoring

Keeps track of the entire tablet assignment operation and can report on its progress and completion
to user issued APIs.

## An example

Let us take our standard example of creating a table in a YugabyteDB universe with 4 nodes. Also, as
before, let us say the table has 16 tablets and a replication factor of 3. The table creation
process is illustrated below. First, the YB-Master leader validates the schema, creates the 16
tablets (48 tablet-peers because of the replication factor of 3) and replicates this data needed for
table creation across a majority of YB-Masters using RAFT.

![create_table_masters](/images/architecture/create_table_masters.png)

Then, the tablets that were created above are assigned to the various YB-TServers.

![tserver_tablet_assignment](/images/architecture/tserver_tablet_assignment.png)

The tablet-peers hosted on different YB-TServers form a RAFT group and elect a leader. For all reads
and writes of keys belonging to this tablet, the tablet-peer leader and the RAFT group are
respectively responsible. Once assigned, the tablets are owned by the YB-TServers until the
ownership is changed by the YB-Master either due to an extended failure or a future load-balancing
event.

For our example, this step is illustrated below.

![tablet_peer_raft_groups](/images/architecture/tablet_peer_raft_groups.png)

Note that henceforth, if one of the YB-TServers hosting the tablet leader fails, the tablet RAFT
group quickly re-elects a leader (in a matter of seconds) for purposes of handling IO. Therefore,
the YB-Master is not in the critical IO path. If a YB-TServer remains failed for an extended period
of time, the YB-Master finds a set of suitable candidates to re-replicate its data to. It does so in
a throttled and graceful manner.
