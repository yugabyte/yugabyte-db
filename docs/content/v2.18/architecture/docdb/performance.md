---
title: DocDB performance enhancements to RocksDB
headerTitle: Performance
linkTitle: Performance
description: Learn how DocDB enhances RocksDB for scale and performance.
menu:
  v2.18:
    identifier: docdb-performance
    parent: docdb
    weight: 1148
type: docs
---

DocDB uses a customized version of [RocksDB](http://rocksdb.org/), a log-structured merge tree (LSM)-based key-value store.

![DocDB Document Storage Layer](/images/architecture/docdb-rocksdb.png)

## Enhancements to RocksDB

The customizations done to RocksDB enhance scalability and improve performance.

### Efficient modelling of documents

The goal of one of the enhancements is to implement a flexible data model on top of a key-value store, as well as to implement efficient operations on this data model such as the following:

* Fine-grained updates to a part of the row or collection without incurring a read-modify-write penalty of the entire row or collection.
* Deleting or overwriting a row, collection, or object at an arbitrary nesting level without incurring a read penalty to determine what specific set of key-value pairs need to be deleted.
* Enforcing row- and object-level TTL-based expiration.

A tighter coupling into the read and compaction layers of the underlying RocksDB key-value store was needed. RocksDB is used as an append-only store, and operations (such as row or collection delete) are modeled as an insert of a special delete marker. This allows deleting an entire subdocument efficiently by adding one key-value pair to RocksDB. Read hooks automatically recognize these markers and suppress expired data. Expired values within the subdocument are cleaned up and garbage-collected by customized compaction hooks.

### Raft vs. RocksDB WAL logs

DocDB uses Raft for replication. Changes to the distributed system are already recorded or journaled as part of Raft logs. When a change is accepted by a majority of peers, it is applied to each tablet peer’s DocDB, but the additional write-ahead logging (WAL) mechanism in RocksDB (under DocDB) was unnecessary and would add overhead. For correctness, in addition to disabling the WAL mechanism in RocksDB, YugabyteDB tracks the Raft sequence ID up to which data has been flushed from RocksDB’s memtables to SSTable files. This ensures that the Raft WAL logs can be correctly garbage-collected. It also allows to replay the minimal number of records from Raft WAL logs on a server crash or restart.

### MVCC at a higher layer

Multi-version concurrency control (MVCC) in DocDB is done at a higher layer and does not use the MVCC mechanism of RocksDB.

The mutations to records in the system are versioned using hybrid timestamps maintained at the YBase layer. As a result, the notion of MVCC as implemented in RocksDB using sequence IDs was not necessary and would only add overhead. YugabyteDB does not use RocksDB’s sequence IDs; instead, it uses hybrid timestamps that are part of the encoded key to implement MVCC.

### Backups and snapshots

Backups and snapshots needed to be higher-level operations that take into consideration data in DocDB, as well as in the Raft logs to obtain a consistent cut of the state of the system.

## Data model-aware bloom filters

The keys stored by DocDB in RocksDB consist of a number of components, where the first component is a document key, followed by a few scalar components, and finally followed by a timestamp (sorted in reverse order).

The bloom filter needs to be aware of what components of the key should be added to the bloom, so that only the relevant SSTable files in the LSM store are being searched during a read operation.

In a traditional key-value store, range scans do not make use of bloom filters because exact keys that fall in the range are unknown. However, a data-model-aware bloom filter was implemented, where range scans within keys that share the same hash component can also benefit from bloom filters. For example, a scan to get all the columns within a row or all the elements of a collection can also benefit from bloom filters.

## Range query optimizations

The ordered (or range) components of the compound-keys in DocDB often have a natural order. For example, it may be an `int` that represents a message ID (for a messaging application) or a timestamp (for a IoT time series). By keeping hints with each SSTable file in the LSM store about the minimum and maximum values for these components of the key, range queries can intelligently prune away the lookup of irrelevant SSTable files during the read operation.

Consider the following example:

```sql
SELECT message_txt
  FROM messages
WHERE user_id = 17
  AND message_id > 50
  AND message_id < 100;
```

The following example illustrates a time series application:

```sql
SELECT metric_value
  FROM metrics
WHERE metric_name = ’system.cpu’
  AND metric_timestamp < ?
  AND metric_timestamp > ?
```

## Efficient memory usage

There are two instances where memory usage across components in a manner that is global to the server yields benefits.

### Server-global block cache

A shared block cache is used across the DocDB and RocksDB instances of all the tablets hosted by a YB-TServer. This maximizes the use of memory resources and avoids creating silos of cache that each need to be sized accurately for different user tables.

### Server-global memstore limits

While per-memstore flush sizes can be configured, in practice, because the number of memstores may change over time as users create new tables or tablets of a table move between servers, a storage engine was enhanced to enforce a global memstore threshold. When such a threshold is reached, selection of which memstore to flush takes into account which memstores carry the oldest records (determined by hybrid timestamps) and, therefore, are holding up Raft logs and preventing them from being garbage-collected.

## Scan-resistant block cache

DocDB enhances RocksDB’s block cache to be scan-resistant. The motivation was to prevent operations such as long-running scans (for example, due to an occasional large query or background Spark jobs) from polluting the entire cache with poor quality data and wiping out useful data.
