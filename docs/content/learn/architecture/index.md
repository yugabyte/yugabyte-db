---
date: 2016-03-09T20:08:11+01:00
title: YugaByte Architecture
weight: 70
---

## About YugaByte

YugaByte is a cloud-native database for mission-critical enterprise applications. It is meant to be
the system-of-record/authoritative database for modern applications. YugaByte allows applications to
easily scale up and scale down in the cloud, on-premise or across hybrid environments without
creating operational complexity or increasing the risk of outages.

In terms of data-model and APIs, YugaByte currently supports **Apache Cassandra CQL** & its
client-drivers natively. In addition to this, it also supports an automatically sharded, clustered &
elastic **Redis-as-a-Database** in a Redis driver compatible manner. Additionally, we are currently
working on **distributed transactions** to support **strongly consistent secondary indexes**,
multi-table/row ACID operations and in general ANSI SQL support.

[TODO: arch diagram]

All of the above data models are powered by a **common underlying core** (a.k.a **YBase**) - a highly
available, distributed system with strongly-consistent write operations, tunable read-consistency
operations, and an advanced log-structured row/documented oriented storage model with several
optimizations for handling ever growing datasets efficiently. Only the upper/edge layer (the
YugaByte Query Layer or **YQL**) of the servers have the API specific aspects - for example, the
server-side implementation of Apache Cassandra and Redis protocols, and the corresponding
query/command compilation and run-time (data type representations, built-in operations, etc.).

In terms of the traditional [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte is a 
CP database (consistent and partition tolerant), but achieves very high availability. The 
architectural design of YugaByte is similar to Spanner, which is also technically a CP system. 
The description about Spanner
[here](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) 
is just as valid for YugaByte. The point is that no system provides 100% availability, so the 
pragmatic question is whether or not the system delivers availability that is so high that most 
users don't worry about its outages. For example, given there are many sources of outages for an 
application, if YugaByte is an insignificant contributor to its downtime, then users are correct to 
not worry about it.

## Overview

A YugaByte cluster, also referred to as a universe, is a group of nodes (VMs, physical machines or
containers) that collectively function as a highly available and resilient database.

A YugaByte universe can be deployed in a variety of configurations depending on business
requirements, and latency considerations. Some examples:

- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)

See here [TODO] for a more elaborate discussion on these deployment options.

A YugaByte *universe* can consist of one or more keyspaces (a.k.a databases in other databases such as
MySQL or Postgres). A keyspace is essentially a namespace and can contain one or more tables.
YugaByte automatically shards, replicates and load-balances these tables across the nodes in the
universe, while respecting user-intent such as cross-AZ or region placement requirements, desired
replication factor, and so on. YugaByte automatically handles failures (e.g., node, AZ or region
failures), and re-distributes and re-replicates data back to desired levels across the remaining
available nodes while still respecting any data placement requirements.

## Core Concepts

### System Overview

A YugaByte universe consists of two sets of processes, YB-Master and YB-TServer. These serve
different purposes. 

The YB-Master processes are responsible for keeping system metadata, coordinating system-wide
operations such as create/alter/drop tables, and initiating maintenance operations such as
load-balancing.  

The YB-TServer (tablet server) processes are responsible for hosting/serving user data (e.g,
tables). 

YugaByte is architected to not have any single point of failure. The YB-Master and YB-TServer
processes use [Raft](https://raft.github.io/), a distributed consensus algorithm, for replicating 
changes to system metadata or user data respectively across a set of nodes. 

High Availability (HA) of the YB-Master’s functionalities and of the user-tables served by the
YB-TServers is achieved by the failure-detection and new-leader election mechanisms that are built
into the Raft implementation.

Below is an illustration of a simple 4-node YugaByte universe:

![4 node cluster](/images/4_node_cluster.png)

Before we dive into the roles of these processes, it is useful to introduce the idea behind how data
is sharded, replicated and persisted.

### Sharding data into tablets

User tables are implicitly managed as multiple shards by the system. These shards are referred to as
**tablets**. The primary key for each row in the table uniquely determines the tablet the row lives in.
For data distribution purposes, a hash based partitioning scheme is used. [Note: For some use cases,
such as ordered secondary indexes, we’ll also support range-partitioned tables. We’ll discuss that
topic in the future.]

The hash space for hash partitioned YugaByte tables is the 2-byte range from 0x0000 to 0xFFFF. Such
a table may therefore have at most 64K tablets. We expect this to be sufficient in practice even for
very large data sets or cluster sizes.

As an example, for a table with 16 tablets the overall hash space [0x0000 to 0xFFFF) is divided into
16 sub-ranges, one for each tablet:  [0x0000, 0x1000), [0x1000, 0x2000), … , [0xF000, 0xFFFF). 

Read/write operations are processed by converting the primary key into an internal key and its hash
value, and determining what tablet the operation should be routed to.

The figure below illustrates this.

![tablet_overview](/images/tablet_overview.png)

For every given key, there is exactly one tablet that owns it. The insert/update/upsert by the end
user is processed by serializing and hashing the primary key into byte-sequences and determining the
tablet they belong to. Let us assume that the user is trying to insert a key k with a value v into a
table T. The figure below illustrates how the tablet owning the key for the above table is
determined.

![tablet_hash](/images/tablet_hash.png)
![tablet_hash_2](/images/tablet_hash_2.png)

### Replicating data with Raft

YugaByte replicates data in order to survive failures while continuing to maintain consistency of
data and not requiring operator intervention. **Replication factor** (or RF) is the number of copies of
data in a YugaByte universe. **Fault tolerance** (or FT) of a YugaByte universe is the maximum number of
node failures it can survive while continuing to preserve correctness of data. Fault tolerance and
replication factor are highly correlated. To achieve a fault tolerance of k nodes, the universe has
to be configured with a replication factor of (2k + 1).

Replication of data in YugaByte is achieved at the level of tablets, using **tablet-peers**. Each tablet
comprises of a set of tablet-peers - each of which stores one copy of the data belonging to the
tablet. There are as many tablet-peers for a tablet as the replication factor, and they form a Raft
group. The tablet-peers are hosted on different nodes to allow data redundancy on node failures.
Note that the replication of data between the tablet-peers is **strongly consistent**.

The figure below illustrates three tablet-peers that belong to a tablet (tablet 1). The tablet-peers
are hosted on different YB-TServers and form a Raft group for leader election, failure detection and
replication of the write-ahead logs.

![raft_replication](/images/raft_replication.png)

The first thing that happens when a tablet starts up is to elect one of the tablet-peers as the
**leader** using the [Raft](https://raft.github.io/) protocol.

Only the leader tablet-peer can process user-facing write and read requests. [Note that while this
is the case for strongly consistent reads, YugaByte offers reading from **followers** with relaxed
guarantees which is desired in some deployment models]. All other tablet-peers are called followers
and merely replicate data, and are available as hot standbys that can take over quickly in case the
leader fails.

The leader tablet-peer of the RAFT group in any tablet translates the user-issued writes into DocDB
updates, which it replicates among the tablet-peers using RAFT to achieve strong consistency. The
set of DocDB updates depends on the user-issued write, and involves locking a set of keys to
establish a strict update order, and optionally reading the older value to modify and update in case
of a read-modify-write operation. The RAFT log is used to ensure that the database state-machine of
a tablet is replicated amongst the tablet-peers with strict ordering and correctness guarantees even
in the face of failures or membership changes. This is essential to achieving strong consistency.

Once the RAFT log is replicated to a majority of tablet-peers and successfully persisted on the
majority, the write is applied into DocDB and is subsequently available for reads. Details of DocDB,
which is a log structured merge-tree (LSM) database, is covered in a subsequent section. Once the
write is persisted on disk by DocDB, the write entries can be purged from the RAFT log. This is done
lazily in the background.

### Persisting data using DocDB

[TODO] Add detailed docdb diagrams.

DocDB is YugaByte’s Log Structured Merge tree (LSM) based storage engine. Once data is replicated
via RAFT across a majority of the tablet-peers, it is applied to each tablet peer’s local DocDB. 

DocDB is a persistent “**key to object/document**” store rather than just a “key to value” store.

* The **keys** in DocDB can be compound keys consisting of:
  * **hash organized components**, followed by
  * **ordered (range) components**. These components are stored in their data type specific sort order; both
    ascending and descending sort order is supported for each ordered component of the key.
* The **values** in DocDB can be:
  * **primitive types**: such as int32, int64, double, text, timestamp, etc.
  * **object types (sorted maps)**: These objects map scalar keys to values, which could be either scalar or
    sorted maps as well.

This model allows multiple levels of nesting, and corresponds to a JSON-like format. Other data
structures like lists, sorted sets etc. are implemented using DocDB’s object type with special key
encodings. In DocDB, timestamps of each update are recorded carefully, so that it is possible to
recover the state of any document at some point in the past.

YugaByte’s DocDB uses a highly customized version of [RocksDB](http://rocksdb.org/), a log-structured merge tree (LSM)
based key-value store. The primary motivation behind the enhancements or customizations to RocksDB
are described below:

* **Efficient implementation of a row/document model on top of a KV store**: To implement a flexible data
  model- such as a row (comprising of columns), or collections types (such as list, map, set) with
  arbitrary nesting - on top of a key-value store, but more importantly to implement efficient
  operations on this data model such as:
  * fine-grained updates to a part of the row or collection without incurring a read-modify-write
    penalty of the entire row or collection
  * deleting/overwriting a row or collection/object at an arbitrary nesting level without incurring a
    read penalty to determine what specific set of KVs need to be deleted
  * enforcing row/object level TTL based expiry
  a tighter coupling into the “read/compaction” layers of the underlying KV store (RocksDB) is needed.
  We use RocksDB as an append-only store and operations such as row or collection delete are modeled
  as an insert of a special “delete marker”.  This allows deleting an entire subdocument efficiently
  by just adding one key/value pair to RocksDB. Read hooks automatically recognize these markers and
  suppress expired data. Expired values within the subdocument are cleaned up/garbage collected by our
  customized compaction hooks.

* **Avoid penalty of two transaction/write-ahead (WAL) logs**: YugaByte is a distributed database that
  uses RAFT for replication. Changes to the distributed system are already recorded/journalled as part
  of RAFT logs. When a change is accepted by a majority of peers, it is applied to each tablet peer’s
  DocDB, but the additional WAL mechanism in RocksDB (under DocDB) is unnecessary and adds overhead.
  For correctness, in addition to disabling the WAL mechanism in RocksDB, YugaByte tracks the RAFT
  “sequence id” up to which data has been flushed from RocksDB’s memtables to SSTable files. This
  ensures that we can correctly garbage collect the RAFT WAL logs as well as replay the minimal number
  of records from RAFT WAL logs on a server crash or restart.

* **Multi-version concurrency control (MVCC) is done at a higher layer**: The mutations to records in the
  system are versioned using hybrid-timestamps maintained at the YBase layer. As a result, the notion
  of MVCC as implemented in a vanilla RocksDB (using sequence ids) is not necessary and only adds
  overhead. YugaByte does not use RocksDB’s sequence ids, and instead uses hybrid-timestamps that are
  part of the encoded key to implement MVCC.

* **Backups/Snapshots**: These need to be higher level operations that take into consideration data in
  DocDB as well as in the RAFT logs to get a consistent cut of the state of the system

* **Read Optimizations**:
  * **Data model aware bloom filters**: The keys stored by DocDB in RocksDB consist of a number of
  components, where the first component is a "document key", followed by a few scalar components, and
  finally followed by a timestamp (sorted in reverse order). 
      * The bloom filter needs to be aware of what components of the key need be added to the bloom so that
        only the relevant SSTable files in the LSM store are looked up during a read operation.
      * In a traditional KV store, range scans do not make use of bloom filters because exact keys that fall
      in the range are unknown. However, we have implemented a data-model aware bloom filter, where range
      scans within keys that share the same hash component can also benefit from bloom filters. For
      example, a scan to get all the columns within row or all the elements of a collection can also
      benefit from bloom filters.

  * **Data model aware range query optimization**: The ordered (or range) components of the compound-keys in
  DocDB frequently have a natural order. For example, it may be a int that represents a message id
  (for a messaging like application) or a timestamp (for a IoT/Timeseries like use case). See example
  below. By keeping hints with each SSTable file in the LSM store about the min/max values for these
  components of the “key”, range queries can intelligently prune away the lookup of irrelevant SSTable
  files during the read operation.

    ```
        SELECT message_txt
          FROM messages
        WHERE user_id = 17
          AND message_id > 50
          AND message_id < 100;
        or,
        SELECT metric_value
          FROM metrics
        WHERE metric_name = ’system.cpu’ 
          AND metric_timestamp < ? 
          AND metric_timestamp > ?
    ```

* **Server-global block cache across multiple DocDB instances**: A shared block cache is used across the
DocDB/RocksDB instances of all the tablets hosted by a YB-TServer. This maximizes the use of memory
resources, and avoids creating silos of cache that each need to be sized accurately for different
user tables.

* **Server-global memstore limits across multiple DocDB instances**: While per-memstore flush sizes can be
configured, in practice, because the number of memstores may change over time as users create new
tables, or tablets of a table move between servers, we have enhanced the storage engine to enforce a
global memstore threshold. When such a threshold is reached, selection of which memstore to flush
takes into account what memstores carry the oldest records (determined using hybrid timestamps) and
therefore are holding up RAFT logs and preventing them from being garbage collected.

* **Scan-resistant block cache**: We have enhanced RocksDB’s block cache to be scan resistant. The
motivation was to prevent operations such as long-running scans (e.g., due to an occasional large
query or background Spark jobs) from polluting the entire cache with poor quality data and wiping
out useful/hot data.

### The YugaByte Query Layer (YQL)

The YQL layer implements the server-side of multiple protocols/APIs that YugaByte supports.
Currently, YugaByte supports Apache Cassandra & Redis wire-protocols natively, and SQL is in the
roadmap.

Every YB-TServer is configured to support these protocols, on different ports. [Port 9042 is the
default port for CQL wire protocol and 6379 is the default port used for Redis wire protocol.] 

From the application perspective this is a stateless layer and the clients can connect to any (one
or more) of the YB-TServers on the appropriate port to perform operations against the YugaByte
cluster.

The YQL layer running inside each YB-TServer implements some of the API specific aspects of each
support API (such as CQL or Redis), but ultimately replicates/stores/retrieves/replicates data using
YugaByte’s common underlying strongly-consistent & distributed core (we refer to as YBase). Some of
the sub-components in YQL are:

* A CQL compiler and execution layer
  * This component includes a “statement cache” - a cache for compiled/execution plan for prepared
statements to avoid overheads associated with repeated parsing of CQL statements.
* A Redis command parser and execution layer
* Support for language specific builtin operations, data type encodings, etc.

![cluster_overview](/images/cluster_overview.png)

### YB-TServer

The YB-TServer (short for YugaByte Tablet Server) is the process that does the actual IO for end
user requests. Recall from the previous section that data for a table is split/sharded into tablets.
Each tablet is composed of one or more tablet-peers depending on the replication factor. And each
YB-TServer hosts one or more tablet-peers.

Note: We will refer to the “tablet-peers hosted by a YB-TServer” simply as the “tablets hosted by a
YB-TServer”.

Below is a pictorial illustration of this in the case of a 4 node YugaByte universe, with one table
that has 16 tablets and a replication factor of 3.

![tserver_overview](/images/tserver_overview.png)

The tablet-peers corresponding to each tablet hosted on different YB-TServers form a RAFT group and
replicate data between each other. The system shown above comprises of 16 independent RAFT groups.
The details of this replication are covered in a previous section on replication.

Within each YB-TServer, there is a lot of cross-tablet intelligence built in to maximize resource
efficiency. Below are just some of the ways the YB-TServer coordinates operations across tablets
hosted by it:

* The block cache is shared across the different tablets in a given YB-TServer. This leads to highly
efficient memory utilization in cases when one tablet is read more often than others. For example,
one table may have a read-heavy usage pattern compared to others. The block cache will automatically
favor blocks of this table as the block cache is global across all tablet-peers.
* The compactions are throttled across tablets in a given YB-TServer to prevent compaction storms.
This prevents the often dreaded high foreground latencies during a compaction storm.
* Compactions are prioritized into large and small compactions with some prioritization to keep the
system functional even in extreme IO patterns.
* Tracks and enforces a global size across all the memstores. This makes sense when there is a skew in
the write rate across tablets. For example, the scenario when there are tablets belonging to
multiple tables in a single YB-TServer and one of the tables gets a lot more writes than the other
tables. The write heavy table is allowed to grow much larger than it could if there was a per-tablet
memory limit, allowing good write efficiency.
* The block cache and memstores represent some of the larger memory-consuming components. Since these
are global across all the tablet-peers, this makes memory management and sizing of these components
across a variety of workloads very easy. In fact, based on the RAM available on the system, the
YB-TServer automatically gives a certain percentage of the total available memory to the block
cache, and another percentage to memstores.


### YB-Master

The YB-Master is the keeper of system meta-data/records, such as what tables exist in the system,
where their tablets live, what users/roles exist, the permissions associated with them, and so on. 

It is also responsible for coordinating background operations (such as load-balancing or initiating
re-replication of under-replicated data) and performing a variety of administrative operations such
as create/alter/drop of a table.

Note that the YB-Master is highly available as it forms a RAFT group with its peers, and it is not
in the critical path of IO against user tables.

![master_overview](/images/master_overview.png)

Here are some of the functions of the YB-Master.

* **Coordinates universe-wide administrative operations** <br> Examples of such operations are user-issued create/alter/drop table requests, as well as a creating
a backup of a table. The YB-Master performs these operations with a guarantee that the operation is
propagated to all tablets irrespective of the state of the YB-TServers hosting these tablets. This
is essential because a YB-TServer failure while one of these universe-wide operations is in progress
cannot affect the outcome of the operation by failing to apply it on some tablets.

* **Stores system metadata** <br> The master stores system metadata such as the information about all the keyspaces, tables, roles,
permissions, and assignment of tablets to YB-TServers. These system records are replicated across
the YB-Masters for redundancy using RAFT as well. The system metadata is also stored as a DocDB
table by the YB-Master(s).

* **Authoritative source of assignment of tablets to YB-TServers** <br> The YB-Master stores all tablets and the corresponding YB-TServers that currently host them. This
map of tablets to the hosting YB-TServers is queried by clients (such as the YQL layer).
Applications using the YB smart clients for various languages (such as Cassandra or Redis) are very
efficient in retrieving data. The smart clients query the YB-Master for the tablet to YB-TServer map
and cache it. By doing so, the smart clients can talk directly to the correct YB-TServer to serve
various queries without incurring additional network hops.

* **Background ops performed throughout the lifetime of the universe**:

  * **Data Placement & Load Balancing**: The YB-Master leader does the initial placement (at CREATE table
time) of tablets across YB-TServers to enforce any user-defined data placement constraints and
ensure uniform load. In addition, during the lifetime of the universe, as nodes are added, fail or
decommissioned, it continues to balance the load and enforce data placement constraints
automatically.

  * **Leader Balancing**: Aside from ensuring that the number of tablets served by each YB-TServer is
balanced across the universe, the YB-Masters also ensures that each node has a symmetric number of
tablet-peer leaders across eligible nodes. 

  * **Re-replication of data on extended YB-TServer failure**:  The YB-Master receives heartbeats from all
the YB-TServers, and tracks their liveness. It detects if any YB-TServers has failed, and keeps
track of the time interval for which the YB-TServer remains in a failed state. If the time duration
of the failure extends beyond a threshold, it finds replacement YB-TServers to which the tablet data
of the failed YB-TServer is re-replicated. Re-replication is initiated in a throttled fashion by the
YB-Master leader so as to not impact the foreground operations of the universe.

## Architecture - Core Operations

This section describes how the YugaByte database works internally in various scenarios.

### Creating a universe

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


### Creating a table

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

### The write IO path

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

### The read IO path

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

### High Availability

As discussed before, YugaByte is a CP database (consistent and partition tolerant) but achieves very
high availability. It achieves this HA by having an active replica that is ready to take over as a
new leader in a matter of seconds after the failure of the current leader and serve requests.

If a node fails, it causes the outage of the processes running on it. These would be a YB-TServer
and the YB-Master (if one was running on that node). Let us look at what happens in each of these
cases.

#### **YB-Master failure**

The YB-Master is not in the critical path of normal IO operations, so its failure will not affect a
functioning universe. Nevertheless, the YB-Master is a part of a RAFT group with the peers running
on different nodes. One of these peers is the active master and the others are active stand-bys. If
the active master (i.e. the YB-Master leader) fails, these peers detect the leader failure and
re-elect a new YB-Master leader which now becomes the active master within seconds of the failure.

#### **YB-TServer failure**

A YB-TServer hosts the YQL layer and a bunch of tablets. Some of these tablets are tablet-peer
leaders that actively serve IOs, and other tablets are tablet-peer followers that replicate data and
are active stand-bys to their corresponding leaders.

Let us look at how the failures of each of the YQL layer, tablet-peer followers and tablet-peer
leaders are handled.

* **YQL Layer failure** <br> Recall that from the application’s perspective, the YQL layer is a stateless. Hence the client that
issued the request just sends the request to the YQL layer on a different node. In the case of
smart-clients, they lookup the ideal YB-TServer location based on the tablet owning the keys, and
send the request directly to that node.

* **Tablet-peer follower failure** <br> The tablet-peer followers are not in the critical path. Their failure does not impact availability
of the user requests.

* **Tablet-peer leader failure** <br> The failure of any tablet-peer leader automatically triggers a new RAFT level leader election within
seconds, and another tablet-peer on a different YB-TServer takes its place as the new leader. The
unavailability window is in the order of a couple of seconds (assuming the default heartbeat
interval of 500 ms) in the event of a failure of the tablet-peer leader.

### Distributed Transactions

[TODO]

## Operational Aspects

### Adding and removing nodes
[Details to be described.]

### Handling node and zone failures
[Details to be described.]

### Zero downtime operations
[Details to be described.]

* Upgrading software versions
* Adding and removing nodes
* Changing machine types on a running universe
* Changing universe deployment

## Architecture - advanced features
[Details to be described]

* Backups
* Bulk importing data with native EMR integration
* Async Replication Support
* Automatic Data Tiering
* Tunable Read Consistency
* Zone aware data-placement

## Comparison with other databases

### YugaByte vs. Apache Cassandra

#### **Data Consistency**
YugaByte is a strongly-consistent system and avoids several pitfalls of an eventually consistent
database.

1. Dirty Reads: Systems with an eventually consistent core offer weaker semantics than even async
replication. To work around this, many Apache Cassandra deployments use quorum read/write modes, but
even those do NOT offer [clean semantics on write
failures](https://stackoverflow.com/questions/12156517/whats-the-difference-between-paxos-and-wr-n-in-cassandra).
2. Another problem due to an eventual consistency core is [deleted values
   resurfacing](https://stackoverflow.com/questions/35392430/cassandra-delete-not-working). 

YugaByte avoids these pitfalls by using a theoretically sound replication model based on RAFT, with
strong-consistency on writes and tunable consistency options for reads.

#### **3x read penalty of eventual consistency**
In eventually consistent systems, anti-entropy, read-repairs, etc. hurt performance and increase
cost. But even in steady state, read operations read from a quorum to serve closer to correct
responses & fix inconsistencies. For a replication factor of the 3, such a system’s read throughput
is essentially ⅓.

*With YugaByte, strongly consistent reads (from leaders), as well as timeline consistent/async reads
from followers perform the read operation on only 1 node (and not 3).*

#### **Read-modify-write operations**
In Apache Cassandra, simple read-modify-write operations such as “increment”, conditional updates,
“INSERT …  IF NOT EXISTS” or “UPDATE ... IF  exists” use a scheme known as light-weight transactions
[which incurs a 4-round-trip
cost](https://teddyma.gitbooks.io/learncassandra/content/concurrent/concurrency_control.html) between replicas. With YugaByte these operations only involve
1-round trip between the quorum members.

#### **Secondary Indexes**
Local secondary indexes in Apache Cassandra ([see
blog](https://pantheon.io/blog/cassandra-scale-problem-secondary-indexes)) require a fan-out read to all nodes, i.e.
index performance keeps dropping as cluster size increases. With YugaByte’s distributed
transactions, secondary indexes will both be strongly-consistent and be point-reads rather than
needing a read from all nodes/shards in the cluster.

#### **Operational Stability / Add Node challenges with Apache Cassandra**

1. Apache Cassandra’s eventually consistent core implies that a new node trying to join a cluster
cannot simply copy data files from current “leader”. A logical read (quorum read) across multiple
surviving peers is needed with Apache Cassandra to build the correct copy of the data to bootstrap a
new node with. These reads need to uncompress/recompress the data back too. With YugaByte, a new
node can be bootstrapped by simply copying already compressed data files from the leader of the
corresponding shard.
2. Apache Cassandra is implemented in Java. GC pauses plus additional problems, especially when running
with large heap sizes (RAM). YugaByte is implemented in C++.
3. Operations like anti-entropy and read-pair hurt steady-state stability of cluster as they consume
additional system resources. With YugaByte, which does replication using distributed consensus,
these operations are not needed since the replicas stay in sync using RAFT or catch up the deltas
cleanly from the transaction log of the current leader.
4. Apache Cassandra needs constant tuning of compactions (because of Java implementation,
non-scaleable, non-partitioned bloom filters and index-metadata, lack of scan-resistant caches, and
so on.).

#### **Operational Flexibility (Moving to new hardware? Want to add a sync/async replica in another region or in public cloud?)**
With YugaByte, these operations are simple 1-click intent based operations that are handled
seamlessly by the system in a completely online manner. YugaByte’s core data fabric and its
consensus based replication model enables the "data tier” to be very agile/recomposable much like
containers/VMs have done for the application or stateless tier.

#### **Addresses need to go beyond single or 2-DC deployments**
The current RDBMS solutions (using async replication) and NoSQL solutions work up to 2-DC
deployments; but in a very restrictive manner. With Apache Cassandra’s 2-DC deployments, one has to
chose between write unavailability on a partition between the DCs, or cope with async replication
model where on a DC failure some inflight data is lost. 

YugaByte’s distributed consensus based replication design, in 3-DC deployments for example, enables
enterprise to “have their cake and eat it too”. It gives use cases the choice to be highly available
for reads and writes even on a complete DC outage, without having to take a down time or resort to
an older/asynchronous copy of the data (which may not have all the changes to the system).

### YugaByte vs. Redis

* YugaByte’s Redis is a persistent database rather than an in-memory cache. [While Redis has a
check-pointing feature for persistence, but it is a highly inefficient operation that does a process
fork. It is also not an incremental operation; the entire memory state is written to disk causing
serious overall performance impact.] 
* YugaByte’s Redis is an auto sharded, clustered with built-in support for strongly consistent
replication and multi-DC deployment flexibility. Operations such as add node, remove node are
simple, throttled and intent-based and leverage YugaByte’s core engine (YBase) and associated
architectural benefits.
* Unlike the normal Redis, the entire data set does not need to fit in memory. In YugaByte, the hot
data lives in RAM, and colder data is automatically tiered to storage and on-demand paged in at
block granularity from storage much like traditional database.
* Applications that use Redis only as a cache and use a separate backing database as the main system
of record, and need to deal with dev pain points around keeping the cache and DB consistent and
operational pain points at two levels of infrastructure (sharding, load-balancing, geo-redundancy)
etc. can leverage YugaByte’s Redis as a unified cache + database offering.
* Scan resistant block cache design ensures long scan (e.g., of older data) do not impact reads for
recent data.

### YugaByte vs. Apache HBase

* **Simpler software stack**: HBase relies on HDFS (another complex piece of infrastructure for data
replication) and on Zookeeper for leader election, failure detection, and so on. Running HBase
smoothly requires a lot of on-going operational overheads.
* **HA / Fast-failover**: In HBase, on a region-server death, the unavailability window of shards/regions
on the server can be in the order of 60 seconds or more. This is because the HBase master first
needs to wait for the server’s ephemeral node in Zookeeper to expire, followed by time take to split
the transaction logs into per-shard recovery logs, and the time taken to replay the edits from the
transaction log by a new server before the shard is available to take IO. In contrast, in YugaByte,
the tablet-peers are hot standbys, and within a matter of few heartbeats (about few seconds) detect
failure of the leader, and initiate leader election.
* **C++ implementation**: Avoids GC tuning; can run better on large memory machines.
Richer data model: YugaByte offers a multi-model/multi-API through CQL & Redis (and SQL in future).
Rather than deal with just byte keys and values, YugaByte offers a rich set of scalar (int, text,
decimal, binary, timestamp, etc.) and composite types (such as collections, UDTs, etc.).
* **Multi-DC deployment** Flexlible deployment choices across multiple DCs or availability zones. HBase provides
strong-consistency only within a single datacenter and offers only async replication alternative for
cross-DC deployments.

## Acknowledgements

[TODO]

