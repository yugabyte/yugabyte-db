---
title: Cache and storage subsystem metrics
headerTitle: Cache and storage subsystems
linkTitle: Cache and storage metrics
headcontent: Monitor metrics for the RocksDB storage subsystem and other caches
description: Learn about YugabyteDB's cache and storage subsystem metrics, and how to select and use the metrics.
menu:
  v2025.2:
    identifier: cache-storage
    parent: metrics-overview
    weight: 120
type: docs
---

## RocksDB storage subsystem metrics

### Storage layer IOPS

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

### Block cache

When the data requested from YSQL layer is sitting in an SST File, it will be cached in RocksDb Block Cache. This is the fundamental cache that sits in RocksDB instead of the YSQL layer. A block requires multiple touches before it is added to the multi-touch (hot) portion of the cache.

The following table describes key cache metrics for the storage (RocksDB) layer.

| Metric | Unit | Type | Description |
| :----- | :--- | :--- | :---------- |
| `rocksdb_block_cache_hit` | blocks | counter | The total number of block cache hits (cache index + cache filter + cache data). |
| `rocksdb_block_cache_miss` | blocks | counter | The total number of block cache misses (cache index + cache filter + cache data). |
| `block_cache_single_touch_usage` | blocks | counter | Blocks of data cached and read once by the YSQL layer are classified in single touch portion of the cache. The size (in bytes) of the cache usage by blocks having a single touch. |
| `block_cache_multi_touch_usage` | blocks | counter | Blocks of data cached and read more than once by the YSQL layer are classified in the multi-touch portion of the cache. The size (in bytes) of the cache usage by blocks having multiple touches. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### Bloom filters

Bloom filters are hash tables used to determine if a given SSTable has the data for a query looking for a particular value.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_bloom_filter_checked` | blocks | counter | The number of times the bloom filter has been checked. |
| `rocksdb_bloom_filter_useful` | blocks | counter | The number of times the bloom filter has avoided file reads (avoiding IOPS). |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### SST files

RocksDB LSM-trees buffer incoming data in a memory buffer that, when full, is sorted, and flushed to disk in the form of a sorted run. When a sorted run is flushed to disk, it may be iteratively merged with existing runs of the same size. Overall, as a result of such iterative merges, the sorted runs on disk (also called Sorted-String Table or SST files) form a collection of levels of exponentially increasing size with potentially overlapping key ranges across the levels.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_current_version_sst_files_size` | bytes | counter | The aggregate size of all SST files. |
| `rocksdb_current_version_num_sst_files` | files | counter | The number of SST files. |
| `ts_active_data_size` |   bytes   |   gauge   | Amount of data in active data directories (excluding snapshots) across all non-hidden tablets. Hidden tablets (retained by a snapshot schedule) are excluded. The gives the size of the data in the cluster as if PITR is off and no snapshots are taken for the databases. |
| `ts_data_size` |   bytes  |   gauge    | Amount of data in data directories (including snapshots) across all tablets. This gives the total size of the data directories including snapshots. To calculate the overhead of snapshots, subtract `ts_active_data_size` from `ts_data_size`. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### Compaction

To make reads more performant over time, RocksDB periodically reduces the number of logical levels by running compaction (sorted-merge) on the SST files in the background, where part or multiple logical levels are merged into one. In other words, RocksDB uses compactions to balance write, space, and read amplifications.

A description of key metrics in this category is listed in the following table:

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_compact_read_bytes` | bytes | counter | Number of bytes being read to do compaction. |
| `rocksdb_compact_write_bytes` | bytes | counter | Number of bytes being written to do compaction. |
| `rocksdb_compaction_times_micros` | microseconds | counter | Time for the compaction process to complete. |
| `rocksdb_numfiles_in_singlecompaction` | files | counter | Number of files in any single compaction. |

### Memtable

Memtable is the first level of data storage where data is stored when you start inserting. It provides statistics about reading documents, which are essentially columns in the table. If a memtable is full, the existing memtable is made immutable and stored on disk as an SST file.

Memtable has statistics about reading documents, which essentially are columns in the table.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_memtable_compaction_micros` | microseconds | counter | Total time to compact a set of SST files. |
| `rocksdb_memtable_hit` | keys | counter | Number of memtable hits. |
| `rocksdb_memtable_miss` | keys | counter | Number of memtable misses. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.

### Write-Ahead-Logging (WAL)

The Write Ahead Log (or WAL) is used to write and persist updates to disk on each tablet. The following table describes metrics for observing the performance of the WAL component.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `log_sync_latency` | microseconds | counter | Time spent to flush (fsync) the WAL entries to disk. |
| `log_wal_sync_overdue_ms` | milliseconds | gauge | Time by which the oldest unsynced WAL entry has exceeded [`interval_durable_wal_write_ms`](../../../../reference/configuration/yb-tserver/#interval-durable-wal-write-ms). The value is 0 when every entry is synced or still within the interval, and when durable WAL writes are enabled or the interval is disabled. When you aggregate tablets, use the maximum. |
| `log_append_latency` | microseconds | counter | Time spent on appending a batch of values to the WAL. |
| `log_group_commit_latency` | microseconds | counter | Time spent on committing an entire group. |
| `log_bytes_logged`| bytes | counter | Number of bytes written to the WAL after the tablet starts. |
| `log_reader_bytes_read` | bytes | counter | Number of bytes read from WAL after the tablet start. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.

### Per-drive write I/O

Available in v2025.2.7.0 and later.

These metrics are exported once per drive (each WAL or data directory) on the `drive` metric entity. They cover Raft WAL writes and RocksDB writes, including SST files, the intents database, tablet metadata, remote bootstrap, and snapshots. `log_sync_latency` is table-level and mixes whatever drives a table's tablets use. These metrics attribute bytes and fsync time to one device.

Read them as rates over a scrape interval. Write throughput is the rate of `drive_bytes_written`. Average fsync cost is the rate of `drive_sync_time` divided by the rate of `drive_sync_count`. Compare `drive_sync_time` with `drive_bytes_written` so a long fsync of a small write is not confused with a long fsync of a large write. Compare a drive with its peers or with its own history.

The metrics are enabled by default. Set [`export_drive_io_metrics`](../../../../reference/configuration/yb-tserver/#export-drive-io-metrics) to `false` to disable them.

| Metric | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `drive_bytes_written` | bytes | counter | Bytes passed to `write()` and `writev()` for files on this drive since the server started. When [`durable_wal_write`](../../../../reference/configuration/yb-tserver/#durable-wal-write) is `true`, writes are block-aligned, so a partially filled trailing block is rewritten on each sync and counted each time. |
| `drive_write_time` | microseconds | counter | Cumulative time spent in `write()` and `writev()` on this drive. Usually small for buffered writes. When `durable_wal_write` is `true`, there is no later fsync, and this counter is where the device cost shows up. |
| `drive_sync_count` | operations | counter | Number of `fsync()` and `fdatasync()` calls for files on this drive since the server started. |
| `drive_sync_time` | microseconds | counter | Cumulative time blocked in `fsync()` and `fdatasync()` on this drive. |
| `drive_range_sync_count` | operations | counter | Number of `sync_file_range()` writeback calls on this drive since the server started. RocksDB uses these to pace SST writeback. |
| `drive_range_sync_time` | microseconds | counter | Cumulative time in `sync_file_range()` writeback on this drive. Counted separately from `drive_sync_time`, because SST writeback can finish before the closing fsync. |
| `drive_bytes_unsynced` | bytes | gauge | Approximate bytes written to this drive and not yet fsynced by YugabyteDB. An upper bound: kernel writeback and `Flush` or `RangeSync` do not decrease it, and `O_DIRECT` writes do not add to it. Compare drives with each other rather than treating the value as an absolute. |
| `drive_sync_latency` | microseconds | counter | Latency of individual `fsync()` and `fdatasync()` calls on this drive. |

## YSQL cache metrics

### Catalog cache misses

During YSQL query processing, system catalog (pg_catalog) tables that live on the YB-Master are cached locally on each YSQL backend process. Misses on this cache can make initial queries or queries after a DDL change slow until the corresponding cache is warmed up. The following table describes metrics for the specific pg_catalog tables that were not found in the cache and required a YB-Master lookup. You can preload these tables using the [ysql_catalog_preload_additional_table_list](../../../../reference/configuration/yb-tserver/#ysql-catalog-preload-additional-table-list) YB-TServer flag; see [Customize preloading of YSQL catalog caches](../../../../best-practices-operations/ysql-catalog-cache-tuning-guide/).

This metric is a counter and units are misses.

| Metric (counter \| misses) | Description |
| :------ | :---------- |
| `handler_latency_yb_ysqlserver_SQLProcessor_CatalogCacheTableMisses_count` | Count of catalog cache misses for this pg_catalog table or an associated index. |
