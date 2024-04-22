---
title: Log structured merge(LSM) tree and Sorted string table (SST)
headerTitle: LSM tree and Sorted string tables (SST)
linkTitle: LSM & SST
description: Internals of how an LSM creates and manages SSTs.
headcontent: Learn the internals of how an LSM creates and manages SSTs
menu:
  preview:
    identifier: docdb-lsm-sst
    parent: docdb
    weight: 300
type: docs
---

A log-structured merge-tree (LSM tree) is a data structure and storage architecture used by [RocksDB](http://rocksdb.org/), the underlying key-value store of DocDB. LSM trees strike a balance between write and read performance, making them suitable for workloads that involve both frequent writes and efficient reads.

The core idea behind an LSM tree is to separate the write and read paths, allowing writes to be sequential and buffered in memory making them faster than random writes, while reads can still access data efficiently through a hierarchical structure of sorted files on disk.

An LSM tree has 2 primary components - Memtable and SSTs. Let's look into each of them in detail and understand how they work during writes and reads.

{{<note>}}
Typically in LSMs there is a third component - WAL (Write ahead log). DocDB uses the Raft logs for this purpose. For more details, see [Raft log vs LSM WAL](../performance/#raft-vs-rocksdb-wal-logs).
{{</note>}}

## Comparison to B-tree

Most traditional databases (for example, MySQL, PostgreSQL, Oracle) have a [B-tree](https://en.wikipedia.org/wiki/B-tree) based storage system. But YugabyteDB had to chose an LSM based storage to build a highly scalable database for of the following reasons.

- Write operations (insert, update, delete) are more expensive in a B-tree. As it involves random writes and in place node splitting and rebalancing. In an LSM-based storage, data is added to the [memtable](#memtable) and written onto a [SST](#sst) file as a batch.
- The append-only nature of LSM makes it more efficient for concurrent write operations.

## Memtable

All new write operations (inserts, updates, and deletes) are written as key-value pairs to an in-memory data structure called a Memtable, which is essentially a sorted map or tree. The key-value pairs are stored in sorted order based on the keys. When the Memtable reaches a certain size, it is made immutable, which means no new writes can be accepted into that Memtable.

The immutable Memtable is then flushed to disk as an SST (Sorted String Table) file. This process involves writing the key-value pairs from the Memtable to disk in a sorted order, creating an SST file. DocDB maintains one active Memtable, and utmost one immutable Memtable at any point in time. This ensures that write operations can continue to be processed in the active Memtable, when the immutable memtable is being flushed to disk.

## SST

Each SST (Sorted String Table) file is an immutable, sorted file containing key-value pairs. The data is organized into data blocks, which are compressed using configurable compression algorithms (for example, Snappy, Zlib). Index blocks provide a mapping between key ranges and the corresponding data blocks, enabling efficient lookup of key-value pairs. Filter blocks containing bloom filters allow for quickly determining if a key might exist in an SST file or not, skipping entire files during lookups. The footer section of an SST file contains metadata about the file, such as the number of entries, compression algorithms used, and pointers to the index and filter blocks.

Each SST file contains a bloom filter, which is a space-efficient data structure that helps quickly determine whether a key might exist in that file or not, avoiding unnecessary disk reads.

{{<note>}}
Most LSMs organize SSTS into multiple levels, where each level contains one or more SST files. But DocDB maintains files in only one level (level0).
{{</note>}}

## Write path

When new data is written to the LSM system, it is first inserted into the active Memtable. As the Memtable fills up, it is made immutable and written to disk as an SST file. Each SST file is sorted by key and contains a series of key-value pairs organized into data blocks, along with index and filter blocks for efficient lookups.

## Read Path

To read a key, the LSM tree first checks the Memtable for the most recent value. If not found, it checks the SST files and finds the key or determines that it doesn't exist. During this process, LSM uses the index and filter blocks in the SST files to efficiently locate the relevant data blocks containing the key-value pairs.

## Compaction

As data accumulates in SSTs, a process called compaction merges and sorts the SST files with overlapping key ranges producing a new set of SST files. The merge process during compaction helps to organize and sort the data, maintaining a consistent on-disk format and reclaiming space from obsolete data versions.

## Learn more

- [Blog: Background Compactions in YugabyteDB](https://www.yugabyte.com/blog/background-data-compaction/#what-is-a-data-compaction)
