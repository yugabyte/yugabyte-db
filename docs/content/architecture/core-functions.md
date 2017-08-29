---
date: 2016-03-09T20:08:11+01:00
title: Architecture - Core Functions
weight: 72
---

This section describes the internal workings of YugaByte DB in the context of the core functions such as write IO path and read IO path.

## Universe creation

When creating a YugaByte universe, the first step is to bring up sufficient YB-Masters (as many as
the replication factor) with each being told about the others. These YB-Masters are brought up for
the first time in the cluster_create mode. This causes them to initialize themselves with a unique
UUID, learn about each other and perform a leader election. Note that subsequent restarts of the
YB-Master, such as after a server crash/restart, do not require the cluster_create option. At the
end of this step, one of the masters establishes itself as the leader.

The next step is to start as many YB-TServers as there are nodes, with the master addresses being
passed to them on startup. They start heart-beating to the masters, communicating the fact that they
are alive. The heartbeats also communicate the tablets the YB-TServers are currently hosting and
their load, but no tablets would exist in the system yet.

Let us illustrate this with our usual example of creating a 4-node YugaByte universe with a
replication factor of 3. In order to do so, first the three masters are started in the create mode
instructing them to that this is a brand new universe create. This is done explicitly to prevent
accidental errors in creating a universe while it is already running.

![create_universe_masters](/images/create_universe_masters.png)

The next step, the masters learn about each other and elect one leader.

![create_universe_master_election](/images/create_universe_master_election.png)

The YB-TServers are then started, and they all heartbeat to the YB-Master.

![create_universe_tserver_heartbeat](/images/create_universe_tserver_heartbeat.png)


## Table creation

The user issued table creation is handled by the YB-Master leader, and is an asychronous API. The
YB-Master leader returns a success for the API once it has replicated both the table schema as well
as all the other information needed to perform the table creation to the other YB-Masters in the
RAFT group to make it resilient to failures.

The table creation is accomplished by the YB-Master leader using the following steps:

* Validates the table schema and creates the desired number of tablets for the table. These tablets
are not yet assigned to YB-TServers.
* Replicates the table schema as well as the newly created (and still unassigned) tablets to the
YB-Master RAFT group. This ensures that the table creation can succeed even if the current YB-Master
leader fails.
* At this point, the asynchronous table creation API returns a success, since the operation can
proceed even if the current YB-Master leader fails.
* Assigns each of the tablets to as many YB-TServers as the replication factor of the table. The
tablet-peer placement is done in such a manner as to ensure that the desired fault tolerance is
achieved and the YB-TServers are evenly balanced with respect to the number of tablets they are
assigned. Note that in certain deployment scenarios, the assignment of the tablets to YB-TServers
may need to satisfy many more constraints such as distributing the individual replicas of each
tablet across multiple cloud providers, regions and availability zones.
* Keeps track of the entire tablet assignment operation and can report on its progress and completion
to user issued APIs.


Let us take our standard example of creating a table in a YugaByte universe with 4 nodes. Also, as
before, let us say the table has 16 tablets and a replication factor of 3. The table creation
process is illustrated below. First, the YB-Master leader validates the schema, creates the 16
tablets (48 tablet-peers because of the replication factor of 3) and replicates this data needed for
table creation across a majority of YB-Masters using RAFT.

![create_table_masters](/images/create_table_masters.png)

Then, the tablets that were created above are assigned to the various YB-TServers.

![tserver_tablet_assignment](/images/tserver_tablet_assignment.png)

The tablet-peers hosted on different YB-TServers form a RAFT group and elect a leader. For all reads
and writes of keys belonging to this tablet, the tablet-peer leader and the RAFT group are
respectively responsible. Once assigned, the tablets are owned by the YB-TServers until the
ownership is changed by the YB-Master either due to an extended failure or a future load-balancing
event.

For our example, this step is illustrated below.

![tablet_peer_raft_groups](/images/tablet_peer_raft_groups.png)

Note that henceforth, if one of the YB-TServers hosting the tablet leader fails, the tablet RAFT
group quickly re-elects a leader (in a matter of seconds) for purposes of handling IO. Therefore,
the YB-Master is not in the critical IO path. If a YB-TServer remains failed for an extended period
of time, the YB-Master finds a set of suitable candidates to re-replicate its data to. It does so in
a throttled and graceful manner.

## The write IO path

For purposes of simplicity, let us take the case of a single key write. The case of distributed
transactions where multiple keys need to be updated atomically is covered in a separate section. 

The user-issued write request first hits the YQL query layer on a port with the appropriate protocol
(Cassandra, Redis, etc). This user request is translated by the YQL layer into an internal key.
Recall from the section on sharding that given a key, it is owned by exactly one tablet. This tablet
as well as the YB-TServers hosting it can easily be determined by making an RPC call to the
YB-Master. The YQL layer makes this RPC call to determine the tablet/YB-TServer owning the key and
caches the result for future use. The YQL layer then issues the write to the YB-TServer that hosts
the leader tablet-peer. The write is handled by the leader of the RAFT group of the tablet owning
the internal key. The leader of the tablet RAFT group does operations such as:

* verifying that the operation being performed is compatible with the current schema
* takes a lock on the key using an id-locker
* reads data if necessary (for read-modify-write or conditional update operations)
* replicates the data using RAFT to its peers
* upon successful RAFT replication, applies the data into its local DocDB
* responds with success to the user

The follower tablets receive the data replicated using RAFT and apply it into their local DocDB once
it is known to have been committed. The leader piggybacks the advancement of the commit point in
subsequent RPCs.

As an example, let us assume the user wants to insert into a table T1 that had a key column K and a
value column V the values (k, v). The write flow is depicted below.

![write_path_io](/images/write_path_io.png)

Note that the above scenario has been greatly simplified by assuming that the user application sends
the write query to a random YugaByte server, which then routes the request appropriately. 

In practice, YugaByte has a **smart client** that can cache the location of the tablet directly and can
therefore save the extra network hop. This allows it to send the request directly to the YQL layer
of the appropriate YB-TServer which hosts the tablet leader. If the YQL layer finds that the tablet
leader is hosted on the local node, the RPC call becomes a library call and saves the work needed to
serialize and deserialize the request.

## The read IO path

As before, for purposes of simplicity, let us take the case of a single key read. The user-issued
read request first hits the YQL query layer on a port with the appropriate protocol (Cassandra,
Redis, etc). This user request is translated by the YQL layer into an internal key. The YQL layer
then finds this tablet as well as the YB-TServers hosting it by making an RPC call to the YB-Master,
and caches the response for future. The YQL layer then issues the read to the YB-TServer that hosts
the leader tablet-peer. The read is handled by the leader of the RAFT group of the tablet owning the
internal key. The leader of the tablet RAFT group which handles the read request performs the read
from its DocDB and returns the result to the user.

Continuing our previous example, let us assume the user wants to read the value where the primary
key column K has a value k from table T1. From the previous example, the table T1 has a key column K
and a value column V. The read flow is depicted below.

![read_path_io](/images/read_path_io.png)

Note that the read queries can be quite complex - even though the example here talks about a simple
key-value like table lookup. The YQL query layer has a full blown query engine to handle queries
which contain expressions, built-in function calls, arithmetic operations in cases where valid, etc.

Also, as mentioned before in the write section, there is a smart YugaByte client which can route the
application requests directly to the correct YB-TServer avoiding any extra network hops or master
lookups.

## High availability

As discussed before, YugaByte is a CP database (consistent and partition tolerant) but achieves very high availability. It achieves this HA by having an active replica that is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

If a node fails, it causes the outage of the processes running on it. These would be a YB-TServer and the YB-Master (if one was running on that node). Let us look at what happens in each of these cases.

### YB-Master failure

The YB-Master is not in the critical path of normal IO operations, so its failure will not affect a functioning universe. Nevertheless, the YB-Master is a part of a RAFT group with the peers running on different nodes. One of these peers is the active master and the others are active stand-bys. If the active master (i.e. the YB-Master leader) fails, these peers detect the leader failure and re-elect a new YB-Master leader which now becomes the active master within seconds of the failure.

### YB-TServer failure

A YB-TServer hosts the YQL layer and a bunch of tablets. Some of these tablets are tablet-peer leaders that actively serve IOs, and other tablets are tablet-peer followers that replicate data and are active stand-bys to their corresponding leaders.

Let us look at how the failures of each of the YQL layer, tablet-peer followers and tablet-peer leaders are handled.

* **YQL Layer failure** <br> Recall that from the application’s perspective, the YQL layer is a stateless. Hence the client that issued the request just sends the request to the YQL layer on a different node. In the case of smart-clients, they lookup the ideal YB-TServer location based on the tablet owning the keys, and send the request directly to that node.

* **Tablet-peer follower failure** <br> The tablet-peer followers are not in the critical path. Their failure does not impact availability of the user requests.

* **Tablet-peer leader failure** <br> The failure of any tablet-peer leader automatically triggers a new RAFT level leader election within seconds, and another tablet-peer on a different YB-TServer takes its place as the new leader. The unavailability window is in the order of a couple of seconds (assuming the default heartbeat interval of 500 ms) in the event of a failure of the tablet-peer leader.

## Distributed transactions

[TODO]
