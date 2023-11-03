---
title: Cache and storage subsystem metrics
headerTitle: Cache and storage subsystems
linkTitle: Cache and storage metrics
headcontent: Monitor RocksDB storage subsystem metrics
description: Learn about YugabyteDB's cache and storage subsystem metrics, and how to select and use the metrics.
menu:
  preview:
    identifier: cache-storage
    parent: metrics-overview
    weight: 120
type: docs
---

## Storage layer IOPS

[DocDB](../../../../architecture/docdb/performance/) uses a modified version of RocksDB (an LSM-based key-value store that consists of multiple logical levels, and data in each level are sorted by key) as the storage layer. This storage layer performs `seek`, `next`, and `prev` operations.

The following table describes key throughput and latency metrics for the storage (RocksDB) layer.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_number_db_next`  | keys | counter | Whenever a tuple is read/updated from the database, a request is made to RocksDB key. Each database operation makes multiple requests to RocksDB. The number of NEXT operations performed to look up a key by RocksDB when a tuple is read/updated by the database. |
| `rocksdb_number_db_prev`  | keys | counter | The number of PREV operations performed to look up a key by RocksDB when a tuple is read/updated from the database. |
| `rocksdb_number_db_seek`  | keys | counter | The number of SEEK operations performed to look up a key by the RocksDB when a tuple is read/updated from the database. |
| `rocksdb_db_write_micros` | microseconds | counter | The time spent by RocksDB in microseconds to write data. |
| `rocksdb_db_get_micros` | microseconds | counter | The time spent by RocksDB in microseconds to retrieve data matching a value. |
| `rocksdb_db_seek_micros`  | microseconds | counter | The time spent by RocksDB in microseconds to retrieve data in a range query. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

## Block cache

When the data requested from YSQL layer is sitting in an SST File, it will be cached in RocksDb Block Cache. This is the fundamental cache that sits in RocksDB instead of the YSQL layer. A block requires multiple touches before it is added to the multi-touch (hot) portion of the cache.

The following table describes key cache metrics for the storage (RocksDB) layer.

| Metric | Unit | Type | Description |
| :----- | :--- | :--- | :---------- |
| `rocksdb_block_cache_hit` | blocks | counter | The total number of block cache hits (cache index + cache filter + cache data). |
| `rocksdb_block_cache_miss` | blocks | counter | The total number of block cache misses (cache index + cache filter + cache data). |
| `block_cache_single_touch_usage` | blocks | counter | Blocks of data cached and read more than once by the YSQL layer are classified in single touch portion of the cache. The size (in bytes) of the cache usage by blocks having a single touch. |
| `block_cache_multi_touch_usage` | blocks | counter | Blocks of data cached and read once by the YSQL layer are classified in the multi-touch portion of the cache. The size (in bytes) of the cache usage by blocks having multiple touches. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

## Bloom filters

Bloom filters are hash tables used to determine if a given SSTable has the data for a query looking for a particular value.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_bloom_filter_checked` | blocks | counter | The number of times the bloom filter has been checked. |
| `rocksdb_bloom_filter_useful` | blocks | counter | The number of times the bloom filter has avoided file reads (avoiding IOPS). |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

## SST files

RocksDB LSM-trees buffer incoming data in a memory buffer that, when full, is sorted, and flushed to disk in the form of a sorted run. When a sorted run is flushed to disk, it may be iteratively merged with existing runs of the same size. Overall, as a result of such iterative merges, the sorted runs on disk (also called Sorted-String Table or SST files) form a collection of levels of exponentially increasing size with potentially overlapping key ranges across the levels.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_current_version_sst_files_size` | bytes | counter | The aggregate size of all SST files. |
| `rocksdb_current_version_num_sst_files` | files | counter | The number of SST files. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

## Compaction

To make reads more performant over time, RocksDB periodically reduces the number of logical levels by running compaction (sorted-merge) on the SST files in the background, where part or multiple logical levels are merged into one. In other words, RocksDB uses compactions to balance write, space, and read amplifications.

A description of key metrics in this category is listed in the following table:

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_compact_read_bytes` | bytes | counter | Number of bytes being read to do compaction. |
| `rocksdb_compact_write_bytes` | bytes | counter | Number of bytes being written to do compaction. |
| `rocksdb_compaction_times_micros` | microseconds | counter | Time for the compaction process to complete. |
| `rocksdb_numfiles_in_singlecompaction` | files | counter | Number of files in any single compaction. |

## Memtable

Memtable is the first level of data storage where data is stored when you start inserting. It provides statistics about reading documents, which are essentially columns in the table. If a memtable is full, the existing memtable is made immutable and stored on disk as an SST file.

Memtable has statistics about reading documents, which essentially are columns in the table.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_memtable_compaction_micros` | microseconds | counter | Total time to compact a set of SST files. |
| `rocksdb_memtable_hit` | keys | counter | Number of memtable hits. |
| `rocksdb_memtable_miss` | keys | counter | Number of memtable misses. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.

## Write-Ahead-Logging (WAL)

The Write Ahead Log (or WAL) is used to write and persist updates to disk on each tablet. The following table describes metrics for observing the performance of the WAL component.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `log_sync_latency` | microseconds | counter | Time spent to flush (fsync) the WAL entries to disk. |
| `log_append_latency` | microseconds | counter | Time spent on appending a batch of values to the WAL. |
| `log_group_commit_latency` | microseconds | counter | Time spent on committing an entire group. |
| `log_bytes_logged`| bytes | counter | Number of bytes written to the WAL after the tablet starts. |
| `log_reader_bytes_read` | bytes | counter | Number of bytes read from WAL after the tablet start. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.
