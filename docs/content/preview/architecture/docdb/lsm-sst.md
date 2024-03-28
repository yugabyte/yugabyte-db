---
title: Log structured merge(LSM) tree and Sorted string table (SST)
headerTitle: LSM tree and Sorted string tables (SST)
linkTitle: LSM & SST
description: Internals of how an LSM creates & manages SSTs
menu:
  preview:
    identifier: docdb-lsm-sst
    parent: docdb
    weight: 300
type: docs
---

A log-structured merge-tree (LSM tree) is a data structure and storage architecture used RocksDB the underlying key-value store of DocDB. LSM trees strike a balance between write and read performance, making them suitable for workloads that involve both frequent writes and efficient reads.

The core idea behind an LSM tree is to separate the write and read paths, allowing writes to be sequential and buffered in memory making them faster than random writes, while reads can still access data efficiently through a hierarchical structure of sorted files on disk.

An LSM tree has 2 primary components - Memtable and SSTs. Let's look into each of them in detail and understand how they work during writes and reads.

## Memtable

All new write operations (inserts, updates, and deletes) are written as key-value pairs to an in-memory data structure called a Memtable, which is essentially a sorted map or tree. The key-value pairs are stored in sorted order based on the keys. When the Memtable reaches a certain size, it is made immutable, which means no new writes can be accepted into that MemTable.

The immutable MemTable is then flushed to disk as an SST (Sorted String Table) file in Level 0 of the LSM structure. This process involves writing the key-value pairs from the MemTable to disk in a sorted order, creating an SST file. DocDB typically maintains a fixed number of MemTables so that another memtable can become active when the immutable one is being flushed to disk. This ensures that write operations can continue to be processed in the active MemTable, when another memtable is being flushed.

## SST

Each SST(Sorted String Table) file is an immutable, sorted file containing key-value pairs. The data is organized into data blocks, which are compressed using configurable compression algorithms (e.g., Snappy, Zlib). Index blocks provide a mapping between key ranges and the corresponding data blocks, enabling efficient lookup of key-value pairs. Filter blocks containing Bloom filters allow for quickly determining if a key might exist in an SST file or not, skipping entire files during lookups. The footer section of an SST file contains metadata about the file, such as the number of entries, compression algorithms used, and pointers to the index and filter blocks.

The SST files are organized into multiple levels (typically 0 through 7), where each level contains one or more SST files. Level 0 contains the most recent SST files, and higher levels contain older and more compacted data.

Each SST file contains a Bloom filter, which is a space-efficient data structure that helps quickly determine whether a key might exist in that file or not, avoiding unnecessary disk reads.

## Write path

When new data is written to the LSM system, it is first inserted into the active MemTable. As the MemTable fills up, it is made immutable and written to disk as an SST file in Level 0. Each SST file in Level 0 is sorted by key and contains a series of key-value pairs organized into data blocks, along with index and filter blocks for efficient lookups.

## Read Path

To read a key, the LSM tree first checks the Memtable for the most recent value. If not found, it checks the SST files in Level 0, then Level 1, and so on, until it finds the key or determines that it doesn't exist. During this process, LSM uses the index and filter blocks within the SST files to efficiently locate the relevant data blocks containing the key-value pairs.

## Compaction

As data accumulates in Level 0 SSTs, a process called compaction merges and sorts the SST files from Level 0 with overlapping key ranges from Level 1, producing a new set of SST files for Level 1. This process continues for higher levels, with lower levels being compacted into higher levels.

The merge process during compaction helps to organize and sort the data, maintaining a consistent on-disk format and reclaiming space from obsolete data versions.

## Learn more

- [Compactions in YugabyteDB - blog](https://www.yugabyte.com/blog/background-data-compaction/#what-is-a-data-compaction)