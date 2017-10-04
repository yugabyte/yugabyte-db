---
date: 2016-03-09T20:08:11+01:00
title: Architecture - Concepts
weight: 71
---

## Overview

YugaByte is a cloud-native database for mission-critical enterprise applications. It is meant to be a system-of-record/authoritative database that applications can rely on for correctness and availability. It allows applications to easily scale up and scale down in the cloud, on-premises or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugaByte currently supports **Apache Cassandra Query Language** & its client drivers natively. In addition, it also supports an automatically sharded, clustered & elastic **Redis-as-a-Database** in a Redis driver compatible manner. **Distributed transactions** to support **strongly consistent secondary indexes**, multi-table/row ACID operations and SQL support is on the roadmap.

![YugaByte Architecture](/images/architecture.png)

All of the above data models are powered by a **common underlying core** (aka **YBase**) - a highly available, distributed system with strong write consistency, tunable read consistency, and an advanced log-structured row/document-oriented storage model with several optimizations for handling ever-growing datasets efficiently. Only the upper/edge layer (the YugaByte Query Layer or **YQL**) of the servers have the API specific aspects - for example, the server-side implementation of Apache Cassandra and Redis protocols, and the corresponding query/command compilation and run-time (data type representations, built-in operations, etc.).

In terms of the traditional [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugaByte is a CP database (consistent and partition tolerant), but achieves very high availability. The architectural design of YugaByte is similar to Google Clouod Spanner, which is also technically a CP system. The description about Spanner [here](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) is just as valid for YugaByte. The point is that no system provides 100% availability, so the pragmatic question is whether or not the system delivers availability that is so high that most users no longer have to be concerned about outages. For example, given there are many sources of outages for an application, if YugaByte is an insignificant contributor to its downtime, then users are correct to not worry about it.

## Universe

A YugaByte cluster, also referred to as a universe, is a group of nodes (VMs, physical machines or containers) that collectively function as a highly available and resilient database.

A YugaByte universe can be deployed in a variety of configurations depending on business requirements, and latency considerations. Some examples:

- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)

A YugaByte *universe* can consist of one or more keyspaces (a.k.a databases in other databases such as MySQL or Postgres). A keyspace is essentially a namespace and can contain one or more tables. YugaByte automatically shards, replicates and load-balances these tables across the nodes in the universe, while respecting user-intent such as cross-AZ or region placement requirements, desired replication factor, and so on. YugaByte automatically handles failures (e.g., node, AZ or region
failures), and re-distributes and re-replicates data back to desired levels across the remaining available nodes while still respecting any data placement requirements.

## Universe components

A YugaByte universe comprises of two sets of processes, YB-Master and YB-TServer. These serve different purposes.

- The YB-Master (aka the YugaByte Master Server) processes are responsible for keeping system metadata, coordinating system-wide operations such as create/alter drop tables, and initiating maintenance operations such as load-balancing.

- The YB-TServer (aka the YugaByte Tablet Server) processes are responsible for hosting/serving user data (e.g, tables).

YugaByte is architected to not have any single point of failure. The YB-Master and YB-TServer processes use [Raft](https://raft.github.io/), a distributed consensus algorithm, for replicating changes to system metadata or user data respectively across a set of nodes.

High Availability (HA) of the YB-Master’s functionalities and of the user-tables served by the YB-TServers is achieved by the failure-detection and new-leader election mechanisms that are built into the Raft implementation.

Below is an illustration of a simple 4-node YugaByte universe:

![4 node cluster](/images/4_node_cluster.png)

Before we dive into the roles of these processes, it is useful to introduce the idea behind how data is sharded, replicated and persisted.

## Data sharding into tablets

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

## Data replication with Raft

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

The leader tablet-peer of the Raft group in any tablet translates the user-issued writes into DocDB
updates, which it replicates among the tablet-peers using Raft to achieve strong consistency. The
set of DocDB updates depends on the user-issued write, and involves locking a set of keys to
establish a strict update order, and optionally reading the older value to modify and update in case
of a read-modify-write operation. The Raft log is used to ensure that the database state-machine of
a tablet is replicated amongst the tablet-peers with strict ordering and correctness guarantees even
in the face of failures or membership changes. This is essential to achieving strong consistency.

Once the Raft log is replicated to a majority of tablet-peers and successfully persisted on the
majority, the write is applied into DocDB and is subsequently available for reads. Details of DocDB,
which is a log structured merge-tree (LSM) database, is covered in a subsequent section. Once the
write is persisted on disk by DocDB, the write entries can be purged from the Raft log. This is done
lazily in the background.

## Data persistence with DocDB

DocDB is YugaByte’s Log Structured Merge tree (LSM) based storage engine. Once data is replicated
via Raft  across a majority of the tablet-peers, it is applied to each tablet peer’s local DocDB.

DocDB is a persistent “**key to object/document**” store rather than just a “key to value” store.

* The **keys** in DocDB are compound keys consisting of:
  * 1 or more **hash organized components**, followed by
  * 0 or more **ordered (range) components**. These components are stored in their data type specific sort order; both
    ascending and descending sort order is supported for each ordered component of the key.
* The **values** in DocDB can be:
  * **primitive types**: such as int32, int64, double, text, timestamp, etc.
  * **object types (sorted maps)**: These objects map scalar keys to values, which could be either scalar or
    sorted maps as well.

This model allows multiple levels of nesting, and corresponds to a JSON-like format. Other data
structures like lists, sorted sets etc. are implemented using DocDB’s object type with special key
encodings. In DocDB, timestamps of each update are recorded carefully, so that it is possible to
recover the state of any document at some point in the past.

YugaByte’s DocDB uses a highly customized version of [RocksDB](http://rocksdb.org/), a
log-structured merge tree (LSM) based key-value store. The primary motivation behind the
enhancements or customizations to RocksDB are described below:

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
  uses Raft for replication. Changes to the distributed system are already recorded/journalled as part
  of Raft logs. When a change is accepted by a majority of peers, it is applied to each tablet peer’s
  DocDB, but the additional WAL mechanism in RocksDB (under DocDB) is unnecessary and adds overhead.
  For correctness, in addition to disabling the WAL mechanism in RocksDB, YugaByte tracks the Raft
  “sequence id” up to which data has been flushed from RocksDB’s memtables to SSTable files. This
  ensures that we can correctly garbage collect the Raft WAL logs as well as replay the minimal number
  of records from Raft WAL logs on a server crash or restart.

* **Multi-version concurrency control (MVCC) is done at a higher layer**: The mutations to records in the
  system are versioned using hybrid-timestamps maintained at the YBase layer. As a result, the notion
  of MVCC as implemented in a vanilla RocksDB (using sequence ids) is not necessary and only adds
  overhead. YugaByte does not use RocksDB’s sequence ids, and instead uses hybrid-timestamps that are
  part of the encoded key to implement MVCC.

* **Backups/Snapshots**: These need to be higher level operations that take into consideration data in
  DocDB as well as in the Raft logs to get a consistent cut of the state of the system

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

    ```sql
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
therefore are holding up Raft logs and preventing them from being garbage collected.

* **Scan-resistant block cache**: We have enhanced RocksDB’s block cache to be scan resistant. The
motivation was to prevent operations such as long-running scans (e.g., due to an occasional large
query or background Spark jobs) from polluting the entire cache with poor quality data and wiping
out useful/hot data.

### DocDB Encoding Overview

DocDB is the storage layer that acts as the common backbone of different APIs that are supported by
YugaByte (currently CQL and Redis).

#### **Mapping Documents to Key-Value Store**

The documents are stored using a key-value store based on RocksDB, which is typeless. The documents
are converted to multiple key-value pairs along with timestamps. Because documents are spread across
many different key-values, it’s possible to partially modify them cheaply.

For example, consider the following document stored in DocDB: 

```
DocumentKey1 = {
	SubKey1 = {
		SubKey2 = Value1
		SubKey3 = Value2
	},
	SubKey4 = Value3
}
```

Keys we store in RocksDB consist of a number of components, where the first component is a "document
key", followed by a few scalar components, and finally followed by a MVCC timestamp (sorted in
reverse order). Each component in the DocumentKey, SubKey, and Value, are PrimitiveValues, which are
just (type, value) pairs, which can be encoded to and decoded from strings. When we encode primitive
values in keys, we use a binary-comparable encoding for the value, so that sort order of the
encoding is the same as the sort order of the value.

Assume that the example document above was written at time T10 entirely. Internally the above
example’s document is stored using 5 RocksDB key value pairs:

```
DocumentKey1, T10 -> {} // This is an init marker
DocumentKey1, SubKey1, T10 -> {}
DocumentKey1, SubKey1, SubKey2, T10 -> Value1
DocumentKey1, SubKey1, SubKey3, T10 -> Value2
DocumentKey1, SubKey4, T10 -> Value3
```

Deletions of Documents and SubDocuments are performed by writing a single Tombstone marker at the
corresponding value. During compaction, overwritten or deleted values are cleaned up to reclaim
space.

#### **Mapping of CQL rows to Documents**

For CQL tables, every row is a document in DocDB. The Document key contains the full primary key -
the values of partition (hash) column(s) and clustering (range) column(s), in order. A 16-bit hash
of the hash portion is also prefixed in the DocKey. The subdocuments within the Document are the
rest of the columns, whose SubKey is the corresponding column ID. If a column is a non-primitive
type (such as a map or set), the corresponding subdocument is an Object.

There’s a unique byte for each data type we support in CQL. The values are prefixed with the
corresponding byte. The type prefix is also present in the primary key’s hash or range components.
We use a binary-comparable encoding to translate the value for each CQL type to strings that go to
the KV-Store.

In CQL there are two types of TTL, the table TTL and column level TTL. The column TTLs are stored
with the value using the same encoding as Redis. The Table TTL is not stored in DocDB (it is stored
in master’s syscatalog as part of the table’s schema). If no TTL is present at the column’s value,
the table TTL acts as the default value.

Furthermore, CQL has a distinction between rows created using Insert vs Update. We keep track of
this difference (and row level TTLs) using a "liveness column", a special system column invisible to
the user. It is added for inserts, but not updates: making sure the row is present even if all
non-primary key columns are deleted only in the case of inserts.

![cql_row_encoding](/images/cql_row_encoding.png)

**CQL Example: Rows with primitive and collection types**

Consider the following CQL commands:

```sql
CREATE TABLE msgs (user_id text,
                   msg_id int,
                   msg text,
                   msg_props map<text, text>,
      PRIMARY KEY ((user_id), msg_id));

T1: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES ('user1', 10, 'msg1', {'from' : 'a@b.com', 'subject' : 'hello'});
```

The entries in DocDB at this point will look like the following:

```
(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
```

```sql
T2: UPDATE msgs
       SET msg_props = msg_props + {'read_status' : 'true'}
     WHERE user_id = 'user1', msg_id = 10
```

The entries in DocDB at this point will look like the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
<b>(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'</b>
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
</code>
</pre>

```sql
T3: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES (‘user1’, 20, 'msg2', {'from' : 'c@d.com', 'subject' : 'bar'});
```

The entries in DocDB at this point will look like the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
<b>(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</b></code>
</pre>

```sql
T4: DELETE msg_props       // Delete a single column from a row
      FROM msgs
     WHERE user_id = 'user1'
       AND msg_id = 10;
```

Even though, in the example above, the column being deleted is a non-primitive column (a map), this
operation only involves adding a delete marker at the correct level, and does not incur any read
overhead. The logical layout in DocDB at this point is shown below.

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
<b>(hash1, 'user1', 10), msg_props_column_id, T4 -> [DELETE]</b>
<s>(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>
*Note: The KVs that are displayed in “strike-through” font are logically deleted.*

Note: The above is not the physical layout per se, as the writes happen in a log-structured manner.
When compactions happen, the space for the KVs corresponding to the deleted columns is reclaimed, as
shown below.

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'

T5: DELETE FROM msgs    // Delete entire row corresponding to msg_id 10
     WHERE user_id = 'user1'
            AND msg_id = 10;

<b>(hash1, 'user1', 10), T5 -> [DELETE]</b>
<s>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>

**CQL Example #2: Time-To-Live (TTL) Handling**

CQL allows the TTL property to be specified at the level of each INSERT/UPDATE operation. In such
cases, the TTL is stored as part of the RocksDB value as shown below:

<pre>
<code>CREATE TABLE page_views (page_id text,
                         views int,
                         category text,
     PRIMARY KEY ((page_id)));

T1: INSERT INTO page_views (page_id, views)
        VALUES ('abc.com', 10)
        <b>USING TTL 86400</b>

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10

T2: UPDATE page_views
     <b>USING TTL 3600</b>
       SET category = 'news'
     WHERE page_id = 'abc.com';

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
<b>(hash1, 'abc.com'), category_column_id, T2 -> (TTL = 3600) 'news'</b></code>
</pre>

**Table Level TTL**: CQL also allows the TTL property to be specified at the table level. In that case,
we do not store the TTL on a per KV basis in RocksDB; but the TTL is implicitly enforced on reads as
well as during compactions (to reclaim space).

#### **Mapping Redis Data to Documents**

Redis is a schemaless data store. There is only one primitive type (string) and some collection
types. In this case, the documents are pretty simple. For primitive values, the document consists of
only one value. The document key is just a string prefixed with a hash. Redis collections are single
level documents. Maps correspond to SubDocuments which are discussed above. Sets are stored as maps
with empty values, and Lists have indexes as keys. For non-primitive values (e.g., hash, set type),
we store the type in parent level initial value, which is sorted before the subkeys. Any redis value
can have a TTL, which is stored in the RocksDB-value.

![redis_docdb_overview](/images/redis_docdb_overview.png)

**Redis Example**

| Timestamp | Command                                   | New Key-Value pairs added in RocksDB                                                                            |
|:---------:|:-----------------------------------------:|:---------------------------------------------------------------------------------------------------------------:|
|T1         |SET key1 value1 EX 15                      |(h1, key1), T1 -> 15, value1                                                                                     |
|T2         |HSET key2 subkey1 value1                   |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T2 -> value1                                        |
|T3         |HSET key2 subkey2 value2                   |(h2, key2), subkey2, T3 -> value2                                                                                |
|T4         |DEL key2                                   |(h2, key2), T4 -> Tombstone                                                                                      |
|T5         |HMSET key2 subkey1 new_val1 subkey3 value3 |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T5 -> new_val1<br/>(h2, key2), subkey3, T5 -> value3|
|T6         |SADD key3 value4 value5                    |(h3, key3), T6 -> [Redis-Set-Type]<br/>(h3, key3), value4, T6 -> [NULL]<br/>(h3, key3), value5, T6 -> [NULL]     |
|T7         |SADD key3 value6                           |(h3, key3), value6, T7 -> [NULL]                                                                                 |

Although they are added out of order, we get a sorted view of the items in the key value store when
reading, as shown below:

```
(h1, key1), T1 -> 15, value1
(h2, key2), T5 -> [Redis-Hash-Type]
(h2, key2), T4 -> Tombstone
(h2, key2), T2 -> [Redis-Hash-Type]
(h2, key2), subkey1, T5 -> new_val1
(h2, key2), subkey1, T2 -> value1
(h2, key2), subkey2, T3 -> value2
(h2, key2), subkey3, T5 -> value3
(h3, key3), T6 -> [Redis-Set-Type]
(h3, key3), value6, T7 -> [NULL]
(h3, key3), value4, T6 -> [NULL]
(h3, key3), value5, T6 -> [NULL]
```

Using an iterator, it is easy to reconstruct the hash and set contents efficiently.

## YugaByte Query Layer (YQL)

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

## YB-TServer

The YB-TServer (short for YugaByte Tablet Server) is the process that does the actual IO for end
user requests. Recall from the previous section that data for a table is split/sharded into tablets.
Each tablet is composed of one or more tablet-peers depending on the replication factor. And each
YB-TServer hosts one or more tablet-peers.

Note: We will refer to the “tablet-peers hosted by a YB-TServer” simply as the “tablets hosted by a
YB-TServer”.

Below is a pictorial illustration of this in the case of a 4 node YugaByte universe, with one table
that has 16 tablets and a replication factor of 3.

![tserver_overview](/images/tserver_overview.png)

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

![master_overview](/images/master_overview.png)

Here are some of the functions of the YB-Master.

### Coordinates universe-wide administrative operations

Examples of such operations are user-issued create/alter/drop table requests, as well as a creating a backup of a table. The YB-Master performs these operations with a guarantee that the operation is propagated to all tablets irrespective of the state of the YB-TServers hosting these tablets. This is essential because a YB-TServer failure while one of these universe-wide operations is in progress cannot affect the outcome of the operation by failing to apply it on some tablets.

### Stores system metadata

The master stores system metadata such as the information about all the keyspaces, tables, roles, permissions, and assignment of tablets to YB-TServers. These system records are replicated across the YB-Masters for redundancy using Raft as well. The system metadata is also stored as a DocDB table by the YB-Master(s).

### Authoritative source of assignment of tablets to YB-TServers

The YB-Master stores all tablets and the corresponding YB-TServers that currently host them. This map of tablets to the hosting YB-TServers is queried by clients (such as the YQL layer). Applications using the YB smart clients for various languages (such as Cassandra or Redis) are very efficient in retrieving data. The smart clients query the YB-Master for the tablet to YB-TServer map and cache it. By doing so, the smart clients can talk directly to the correct YB-TServer to serve various queries without incurring additional network hops.

### Background ops performed throughout the lifetime of the universe

#### Data Placement & Load Balancing

The YB-Master leader does the initial placement (at CREATE table time) of tablets across YB-TServers to enforce any user-defined data placement constraints and ensure uniform load. In addition, during the lifetime of the universe, as nodes are added, fail or
decommissioned, it continues to balance the load and enforce data placement constraints automatically.

#### Leader Balancing

Aside from ensuring that the number of tablets served by each YB-TServer is balanced across the universe, the YB-Masters also ensures that each node has a symmetric number of tablet-peer leaders across eligible nodes.

#### Re-replication of data on extended YB-TServer failure

The YB-Master receives heartbeats from all the YB-TServers, and tracks their liveness. It detects if any YB-TServers has failed, and keeps track of the time interval for which the YB-TServer remains in a failed state. If the time duration of the failure extends beyond a threshold, it finds replacement YB-TServers to which the tablet data of the failed YB-TServer is re-replicated. Re-replication is initiated in a throttled fashion by the YB-Master leader so as to not impact the foreground operations of the universe.

## Acknowledgements

The YugaByte code base has leveraged several open source projects as a starting point.

* Google Libraries (glog, gflags, protocol buffers, snappy, gperftools, gtest, gmock).

* The DocDB layer uses a highly customized/enhanced version of RocksDB. A sample of the
  customizations and enhancements we have done are described [in this
  section](#data-persistence-with-docdb).

* We used Apache Kudu's Raft implementation & the server framework as a starting point. Since then,
  we have implemented several enhancements such as leader-leases & pre-voting state during learner
  mode for correctness, improvements to the network stack, auto balancing of tablets on failures,
  zone/DC aware data placement, leader-balancing, ability to do full cluster moves in a online
  manner, and so on to name just a few.

* Postgres' scanner/parser modules (.y/.l files) were used as a starting point for implementating
  CQL (Apache Cassandra Query Language) scanner/parser in C++.
