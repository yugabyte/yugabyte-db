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
  * system are versioned using hybrid-timestamps maintained at the YBase layer. As a result, the notion
  * of MVCC as implemented in a vanilla RocksDB (using sequence ids) is not necessary and only adds
  * overhead. YugaByte does not use RocksDB’s sequence ids, and instead uses hybrid-timestamps that are
  * part of the encoded key to implement MVCC.

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

* **Coordinates universe-wide administrative operations**

Examples of such operations are user-issued create/alter/drop table requests, as well as a creating
a backup of a table. The YB-Master performs these operations with a guarantee that the operation is
propagated to all tablets irrespective of the state of the YB-TServers hosting these tablets. This
is essential because a YB-TServer failure while one of these universe-wide operations is in progress
cannot affect the outcome of the operation by failing to apply it on some tablets.

* **Stores system metadata**

The master stores system metadata such as the information about all the keyspaces, tables, roles,
permissions, and assignment of tablets to YB-TServers. These system records are replicated across
the YB-Masters for redundancy using RAFT as well. The system metadata is also stored as a DocDB
table by the YB-Master(s).

* **Authoritative source of assignment of tablets to YB-TServers**

The YB-Master stores all tablets and the corresponding YB-TServers that currently host them. This
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

