---
title: Universe, Cluster, YB-TServer, YB-Master
linkTitle: Universe, Cluster, YB-TServer, YB-Master
description: Universe, Cluster, YB-TServer, YB-Master
menu:
  v1.0:
    identifier: architecture-universe
    parent: architecture-concepts
    weight: 930
---

## Universe

### Introduction

A YugaByte universe, is a group of nodes (VMs, physical machines or containers) that collectively function as a highly available and resilient database.

The universe can be deployed in a variety of configurations depending on business requirements, and latency considerations. Some examples:

- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)

A YugaByte *universe* can consist of one or more keyspaces (a.k.a databases in other databases such as MySQL or Postgres). A keyspace is essentially a namespace and can contain one or more tables. YugaByte automatically shards, replicates and load-balances these tables across the nodes in the universe, while respecting user-intent such as cross-AZ or region placement requirements, desired replication factor, and so on. YugaByte automatically handles failures (e.g., node, process, AZ or region failures), and re-distributes and re-replicates data back to desired levels across the remaining available nodes while still respecting any data placement requirements.

### Processes

A YugaByte universe comprises of two sets of processes, YB-Master and YB-TServer. These serve different purposes.

- The YB-Master (aka the YugaByte Master Server) processes are responsible for keeping system metadata, coordinating system-wide operations such as create/alter drop tables, and initiating maintenance operations such as load-balancing.

- The YB-TServer (aka the YugaByte Tablet Server) processes are responsible for hosting/serving user data (e.g, tables).

YugaByte is architected to not have any single point of failure. The YB-Master and YB-TServer processes use [Raft](https://raft.github.io/), a distributed consensus algorithm, for replicating changes to system metadata or user data respectively across a set of nodes.

High Availability (HA) of the YB-Master’s functionalities and of the user-tables served by the YB-TServers is achieved by the failure-detection and new-leader election mechanisms that are built into the Raft implementation.

Below is an illustration of a simple 4-node YugaByte universe:

![4 node cluster](/images/architecture/4_node_cluster.png)

## Cluster
A YugaByte Universe can comprise of one or more clusters. Each cluster is a logical group of nodes running YB-TServers that are either performing strong (synchronous) replication of the user data or are in a timeline consistent (asynchronous) replication mode. The set of nodes that are performing strong replication are referred to as the Primary cluster and other groups are called Read Replica clusters. There is always one primary cluster in a universe and there can be zero or more read replica clusters in that universe. More information is [here](../replication/#read-only-replicas).

Note: In most of the docs, the term `cluster` and `universe` are used interchangeably.

## YB-TServer

The YB-TServer (short for YugaByte Tablet Server) is the process that does the actual IO for end
user requests. Recall from the previous section that data for a table is split/sharded into tablets.
Each tablet is composed of one or more tablet-peers depending on the replication factor. And each
YB-TServer hosts one or more tablet-peers.

Note: We will refer to the “tablet-peers hosted by a YB-TServer” simply as the “tablets hosted by a
YB-TServer”.

Below is a pictorial illustration of this in the case of a 4 node YugaByte universe, with one table
that has 16 tablets and a replication factor of 3.

![tserver_overview](/images/architecture/tserver_overview.png)

The tablet-peers corresponding to each tablet hosted on different YB-TServers form a Raft group and
replicate data between each other. The system shown above comprises of 16 independent Raft groups.
The details of this replication are covered in a previous section on replication.

Within each YB-TServer, there is a lot of cross-tablet intelligence built in to maximize resource
efficiency. Below are just some of the ways the YB-TServer coordinates operations across tablets
hosted by it:

* **Server-global block cache**: The block cache is shared across the different tablets in a given
YB-TServer. This leads to highly efficient memory utilization in cases when one tablet is read more
often than others. For example, one table may have a read-heavy usage pattern compared to
others. The block cache will automatically favor blocks of this table as the block cache is global
across all tablet-peers.

* **Throttled Compactions**: The compactions are throttled across tablets in a given YB-TServer to
prevent compaction storms. This prevents the often dreaded high foreground latencies during a
compaction storm.

* **Small/Large Compaction Queues**: Compactions are prioritized into large and small compactions with
some prioritization to keep the system functional even in extreme IO patterns.

* **Server-global Memstore Limit**: Tracks and enforces a global size across the memstores for
different tablets. This makes sense when there is a skew in the write rate across tablets. For
example, the scenario when there are tablets belonging to multiple tables in a single YB-TServer and
one of the tables gets a lot more writes than the other tables. The write heavy table is allowed to
grow much larger than it could if there was a per-tablet memory limit, allowing good write
efficiency.

* **Auto-Sizing of Block Cache/Memstore** The block cache and memstores represent some of the larger
memory-consuming components. Since these are global across all the tablet-peers, this makes memory
management and sizing of these components across a variety of workloads very easy. In fact, based on
the RAM available on the system, the YB-TServer automatically gives a certain percentage of the
total available memory to the block cache, and another percentage to memstores.

* **Striping tablet load uniformly across data disks** On multi-SSD machines, the data (SSTable) and
WAL (RAFT write-ahead-log) for various tablets of tables are evenly distributed across the attached
disks on a **per-table basis**. This ensures that each disk handles an even amount of load for each
table.


## YB-Master

The YB-Master is the keeper of system meta-data/records, such as what tables exist in the system, where their tablets live, what users/roles exist, the permissions associated with them, and so on.

It is also responsible for coordinating background operations (such as load-balancing or initiating re-replication of under-replicated data) and performing a variety of administrative operations such as create/alter/drop of a table.

Note that the YB-Master is highly available as it forms a Raft group with its peers, and it is not in the critical path of IO against user tables.

![master_overview](/images/architecture/master_overview.png)

Here are some of the functions of the YB-Master.

### Coordinates universe-wide administrative operations

Examples of such operations are user-issued create/alter/drop table requests, as well as a creating a backup of a table. The YB-Master performs these operations with a guarantee that the operation is propagated to all tablets irrespective of the state of the YB-TServers hosting these tablets. This is essential because a YB-TServer failure while one of these universe-wide operations is in progress cannot affect the outcome of the operation by failing to apply it on some tablets.

### Stores system metadata

The master stores system metadata such as the information about all the keyspaces, tables, roles, permissions, and assignment of tablets to YB-TServers. These system records are replicated across the YB-Masters for redundancy using Raft as well. The system metadata is also stored as a DocDB table by the YB-Master(s).

### Authoritative source of assignment of tablets to YB-TServers

The YB-Master stores all tablets and the corresponding YB-TServers that currently host them. This map of tablets to the hosting YB-TServers is queried by clients (such as the YQL layer). Applications using the YB smart clients for various languages (such as Cassandra, Redis, or PostgreSQL(beta)) are very efficient in retrieving data. The smart clients query the YB-Master for the tablet to YB-TServer map and cache it. By doing so, the smart clients can talk directly to the correct YB-TServer to serve various queries without incurring additional network hops.

### Background ops performed throughout the lifetime of the universe

#### Data Placement & Load Balancing

The YB-Master leader does the initial placement (at CREATE table time) of tablets across YB-TServers to enforce any user-defined data placement constraints and ensure uniform load. In addition, during the lifetime of the universe, as nodes are added, fail or
decommissioned, it continues to balance the load and enforce data placement constraints automatically.

#### Leader Balancing

Aside from ensuring that the number of tablets served by each YB-TServer is balanced across the universe, the YB-Masters also ensures that each node has a symmetric number of tablet-peer leaders across eligible nodes.

#### Re-replication of data on extended YB-TServer failure

The YB-Master receives heartbeats from all the YB-TServers, and tracks their liveness. It detects if any YB-TServers has failed, and keeps track of the time interval for which the YB-TServer remains in a failed state. If the time duration of the failure extends beyond a threshold, it finds replacement YB-TServers to which the tablet data of the failed YB-TServer is re-replicated. Re-replication is initiated in a throttled fashion by the YB-Master leader so as to not impact the foreground operations of the universe.
