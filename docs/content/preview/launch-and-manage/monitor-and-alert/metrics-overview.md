---
title: YugabyteDB metrics
headerTitle: Metrics
linkTitle: Metrics
headcontent: YugabyteDB's database metrics and troubleshooting system, table, and tablet issues.
description: Learn about YugabyteDB's database metrics, and how to select and use the metrics relevant to your situation
menu:
  preview:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 200
type: docs
---

YugabyteDB provides nearly two thousand metrics for monitoring, performance-tuning, and troubleshooting system, table, and tablet issues.

This document focuses primarily on metrics and their uses detailing about which metrics are the most important ones, how to consume the metrics in practice, and how to use them to monitor and manage clusters, troubleshoot performance issues, and identify bottlenecks.

{{< note title="Note" >}}

- This page does not contain an exhaustive list of all the metrics exported by YugabyteDB.
- This page only covers metrics and not query tuning. To learn more about query tuning in YugabyteDB, please visit this page.

{{< /note >}}

## Overview

A YugabyteDB cluster comprises multiple nodes and services, each emitting metrics. These metrics are applicable at different levels: cluster-wide, per-node, per-table, and per-tablet. These metrics are exported through various endpoints in JSON, HTML, and Prometheus formats. You can aggregate these metrics appropriately to get a cluster-wide, per-database, per-table, or per-node view. The following diagram shows the various endpoints in a cluster.

<!-- ### Logical architecture components -->

The following table includes a brief description of the type of metrics exposed by each endpoint and the URL from which their metrics can be exported.

| End-point name | Description | JSON format | Prometheus format |
| :------------ | :---------- | :---------- | :---------------------------------------- |
| [YB-Master](../../../architecture/concepts/yb-master/) | YB-Master exposes metrics related to the system catalog, cluster-wide metadata (such as the number of tablets and tables), and cluster-wide operations (table creations/drops, and so on.). | `<node-ip>:7000/metrics` | `<node-ip>:7000/prometheus-metrics` |
| [YB-TServer](../../../architecture/concepts/yb-tserver/) | YB-Tserver exposes metrics related to end-user DML requests (such as table insert), which include tables, tablets, and storage-level metrics (such as Write-Ahead-Logging, and so on.). | `<node-ip>:9000/metrics` | `<node-ip>:9000/prometheus-metrics` |
| [YSQL](../../../api/ysql/) | This endpoint exposes the YSQL query processing and connection metrics, such as throughput and latencies for the various operations. | `<node-ip>:13000/metrics` | `<node-ip>:13000/prometheus-metrics` |
| [YCQL](../../../api/ycql/) | This endpoint exposes the YCQL query processing and connection metrics (such as throughput and latencies for various operations). | `<node-ip>:12000/metrics` | `<node-ip>:12000/prometheus-metrics` |

{{< note title="Note" >}}

System-level metrics are not exposed by YugabyteDB and are generally collected using an external tool such as [node_exporter](https://prometheus.io/docs/guides/node-exporter/) if using Prometheus. If you use products that automate metrics and monitoring (such as YugabyteDB Anywhere and YugabyteDB Managed), the node-level metric collection and aggregating to expose the key metrics is done out of the box.

{{< /note >}}

### Metric categories

YugabyteDB has four major types of metrics per node: [Server](https://github.com/yugabyte/yugabyte-db/diffs/3?base_sha=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&commentable=true&head_user=polarweasel&name=doc-12-add-metrics-overview&pull_number=16107&sha1=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&sha2=f3bb86c03f3927df7c022a44ce9d430e412f7e96&short_path=fc1ba5b&unchanged=expanded&w=false#server-metrics), [Table](https://github.com/yugabyte/yugabyte-db/diffs/3?base_sha=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&commentable=true&head_user=polarweasel&name=doc-12-add-metrics-overview&pull_number=16107&sha1=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&sha2=f3bb86c03f3927df7c022a44ce9d430e412f7e96&short_path=fc1ba5b&unchanged=expanded&w=false#table-metrics), [Tablet](https://github.com/yugabyte/yugabyte-db/diffs/3?base_sha=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&commentable=true&head_user=polarweasel&name=doc-12-add-metrics-overview&pull_number=16107&sha1=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&sha2=f3bb86c03f3927df7c022a44ce9d430e412f7e96&short_path=fc1ba5b&unchanged=expanded&w=false#tablet-metrics), and [Cluster](https://github.com/yugabyte/yugabyte-db/diffs/3?base_sha=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&commentable=true&head_user=polarweasel&name=doc-12-add-metrics-overview&pull_number=16107&sha1=b8c7b3ba3bd5f78029bcca06b495322d2eb1a9f3&sha2=f3bb86c03f3927df7c022a44ce9d430e412f7e96&short_path=fc1ba5b&unchanged=expanded&w=false#cluster-metrics).

These metric names are generally of the form `<metric_category>_<server_type>_<service_type>_<service_method>` where:

`metric_category` (optional) can be one of:

- `handler_latency` - The latency seen by the logical architecture block.
- `rpcs_in_queue` - The number of RPCs in the queue for service.
- `service_request_bytes` - The number of bytes a service sends to other services in a request. This metric type is beneficial in a very limited number of cases.
- `service_response_bytes` - The number of bytes a service receives from other services in a request. This metric type is beneficial in a very limited number of cases.
- `proxy_request_bytes` - The number of request bytes sent by the proxy in a request to a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. This metric type is beneficial in a very limited number of cases.
- `proxy_response_bytes` - The number of response bytes the proxy receives from a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. This metric type is beneficial in a very limited number of cases.

`server_type` must be one of the following:

- `yb_tserver` - YB-TServer metrics
- `yb_master` - YB-Master metrics
- `yb_ycqlserver` - YCQL metrics
- `yb_ysqlserver` - YSQL metrics
- `yb_consesus` - RAFT consensus metrics
- `yb_cdc` - Change Data Capture metrics

`service_method` (optional) identifies service methods, which are specific functions performed by the service.

{{< note title= "Note">}}

While the preceding metric name syntax is generally adopted, YugabyteDB exports other server metrics which do not conform to the syntax.

{{< /note >}}

## Throughput and latencies

### YSQL query processing

YSQL query processing metrics represent the total inclusive time it takes YugabyteDB to process a YSQL statement after the query processing layer begins execution. These metrics include the time taken to parse and execute the SQL statement, replicate over the network, the time spent in the storage layer, and so on. The preceding metrics do not capture the time to deserialize the network bytes and parse the query.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_ysqlserver_SQLProcessor_InsertStmt` | microseconds | counter | The time in microseconds to parse and execute INSERT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` | microseconds | counter | The time in microseconds to parse and execute SELECT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_UpdateStmt` | microseconds | counter | The time in microseconds to parse and execute UPDATE statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_BeginStmt` | microseconds | counter | The time in microseconds to parse and execute transaction BEGIN statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_CommitStmt` | microseconds | counter | The time in microseconds to parse and execute transaction COMMIT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_RollbackStmt` | microseconds | counter | The time in microseconds to parse and execute transaction ROLLBACK statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_OtherStmts` | microseconds | counter | The time in microseconds to parse and execute all other statements apart from the preceding ones listed in this table. This includes statements like PREPARE, RELEASE SAVEPOINT, and so on. |
| `handler_latency_yb_ysqlserver_SQLProcessor_Transactions` | microseconds | counter | The time in microseconds to execute any of the statements in this table.|


**Note** that all YugabyteDB handler latency metrics have ten additional attributes in them which include the following:

- `total_count/count` - The number of times the value of a metric has been measured.
- `min` - The minimum value of a metric across all measurements.
- `mean` - The average value of the metric across all measurements.
- `Percentile_75` - The 75th percentile value of the metric across all measurements.
- `Percentile_95` - The 95th percentile value of the metric across all measurements.
- `Percentile_99` - The 99th percentile of the metric across all metrics measurements.
- `Percentile_99_9` - The 99.9th percentile of the metric across all metrics measurements.
- `Percentile_99_99` - The 99.99th percentile of the metric across all metrics measurements.
- `max` - The maximum value of the metric across all measurements.
- `total_sum/sum` - The aggregate of all the metric values across the measurements reflected in total_count/count.

For example, if a `select * from table` is executed once and returns 8 rows in 10 milliseconds, then the `total_count=1`, `total_sum=10`, `min=6`, `max=10`, and `mean=8`. If the same query is run again and returns in 6 milliseconds, then the `total_count=2`, `total_sum=16`, `min=6`, `max=10`, and `mean=8`.

Even though these attributes are present in all `handler_latency` metrics, they may not be calculated for all the metrics.

The YSQL throughput can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.

### Database IOPS (reads and writes)

The [YB-TServer](../../../architecture/concepts/yb-tserver/) is responsible for the actual I/O of client requests in a YugabyteDB cluster. Each node in the cluster has a YB-TServer, and each hosts one or more tablet peers.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | The time in microseconds to perform WRITE operations at a tablet level |
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | The time in microseconds to perform WRITE operations at a tablet level |

These metrics can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.

## Connections

Connection metrics represent the cumulative number of connections to YSQL backend per node. This includes various background connections, such as checkpointer, active connections count that only includes the client backend connections, newly established connections, and connections rejected over the maximum connection limit. By default, YugabyteDB can have up to 10 simultaneous connections per vCPU. Connection metrics are only available in Prometheus format.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `yb_ysqlserver_active_connection_total` | connections | counter | The number of active client backend connections to YSQL. |
| `yb_ysqlserver_connection_total` | connections | counter | The number of all connections to YSQL. |
| `yb_ysqlserver_max_connection_total` | connections | counter | The number of maximum connections that can be supported by a node at a given time. |
| `yb_ysqlserver_connection_over_limit_total` | connections | counter | The number of connections rejected over the maximum connection limit has been reached. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

## Replication metrics

### xCluster

YugabyteDB allows you to asynchronously replicate data between independent YugabyteDB clusters.

The replication lag metrics are computed at a tablet level as the difference between Hybrid Logical Clock (HLC) time on the source's tablet server, and the hybrid clock timestamp of the latest record pulled from the source.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `async_replication_committed_lag_micros` | microseconds | counter | The time in microseconds for the replication lag on the target cluster. This metric is available only on the source cluster. |
| `time_since_last_getchanges` | microseconds | counter | The time elapsed in microseconds from when the source cluster got a request to replicate from the target cluster. This metric is available only on the source cluster. |
| `consumer_safe_time_lag` | | | The time elapsed in microseconds between the physical time and safe time. Safe time is when data has been replicated to all the tablets on the consumer cluster. This metric is available only on the target cluster. |
| `consumer_safe_time_skew` | | | The time elapsed in microseconds for replication between the first and the last tablet replica on the consumer cluster. This metric is available only on the target cluster. |

## Cache and storage subsystems

### Storage layer IOPS

[DocDB](../../../architecture/docdb/performance/) uses a modified version of RocksDB (an LSM-based key-value store that consists of multiple logical levels, and data in each level are sorted by key) as the storage layer. This storage layer performs `seek`, `next`, and `prev` operations.

A description of throughput and latency metrics for the storage (RocksDB) layer is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `Rocksdb_number_db_next`  | keys | counter | Whenever a tuple is read/updated from the database, a request is made to RocksDB key. Each database operation will have multiple requests to RocksDB. The number of NEXT operations performed to lookup a key by RocksDB when a tuple is read/updated by the database. |
| `Rocksdb_number_db_prev`  | keys | counter | The number of PREV operations performed to lookup a key by RocksDB when a tuple is read/updated from the database. |
| `Rocksdb_number_db_seek`  | keys | counter | The number of SEEK operations performed to lookup a key by the RocksDB when a tuple is read/updated from the database. |
| `Rocksdb_db_write_micros` | microseconds | counter | The time spent by RocksDB in microseconds to write data |
| `Rocksdb_db_get_micros` | microseconds | counter | The time spent by RocksDB in microseconds to retrieve data matching a value |
| `Rocksdb_db_seek_micros`  | microseconds | counter | The time spent by RocksDB in microseconds to retrieve data in a range query |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### Block cache

When the data requested from YSQL layer is sitting in an SST File, it will be cached in RocksDb Block Cache. This is the fundamental cache that sits in RocksDB instead of YSQL layer. A block requires multiple touches before it is added to the multi-touch (hot) portion of the cache.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_block_cache_hit` | blocks | counter | The total number of block cache hits (cache index + cache filter + cache data) |
| `rocksdb_block_cache_miss` | blocks | counter | The total number of block cache misses (cache index + cache filter + cache data) |
| `block_cache_single_touch_usage` | blocks | counter | Blocks of data cached and read once by the YSQL layer are classified in single touch portion of the cache. The size (in bytes) of the cache usage by blocks having a single touch. |
| `block_cache_multi_touch_usage` | blocks | counter | Blocks of data cached and read once by the YSQL layer are classified in the multi-touch portion of the cache. The size (in bytes) of the cache usage by blocks having multiple touches. |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### Bloom filters

Bloom filters are hash tables used to determine if a given SSTable has the data for a query looking for a particular value.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_bloom_filter_checked` | blocks | counter | The number of times the bloom filter has been checked. |
| `rocksdb_bloom_filter_useful` | blocks | counter | The number of times the bloom filter has avoided file reads (avoiding IOPS). |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### SST files

RocksDB LSM-trees buffer incoming data in a memory buffer that, when full, is sorted, and flushed to disk in the form of a sorted run. When a sorted run is flushed to disk, it may be iteratively merged with existing runs of the same size. Overall, as a result of such iterative merges, the sorted runs on disk, also termed Sorted-String Table or SST files, form a collection of levels of exponentially increasing size with potentially overlapping key ranges across the levels.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_current_version_sst_files_size` | bytes | counter | The aggregate size of all SST files |
| `rocksdb_current_version_num_sst_files` | files | counter | The number of SST files |

These metrics can be aggregated across the entire cluster using appropriate aggregations.

### Compaction

To make reads more performant over time, RocksDB periodically reduces the number of logical levels by running compaction (sorted-merge) on the SST files in the background, where part or multiple logical levels are merged into one. In other words, RocksDB uses compactions to balance write, space, and read amplifications.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_compact_read_bytes` | bytes | counter | The number of bytes being read to do compaction. |
| `rocksdb_compact_write_bytes` | bytes | counter | The number of bytes being written to do compaction. |
| `rocksdb_compaction_times_micros` | microseconds | counter | The time in microseconds for the compaction process to complete. |
| `rocksdb_numfiles_in_singlecompaction` | files | counter | The number of files in any single compaction. |

### Memtable

Memtable is the first level of data storage where data is stored when you start inserting. It provides statistics about reading documents, which are essentially columns in the table. If a memtable is full, the existing memtable is made immutable and stored on disk as an SST file.

Memtable has statistics about reading documents, which essentially are columns in the table.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `rocksdb_memtable_compaction_micros` | microseconds | counter | The total time in microseconds to compact an set of SST files. |
| `rocksdb_memtable_hit` | keys | counter | The number of memtable hits. |
| `rocksdb_memtable_miss` | keys | counter | The number of memtable misses. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.

### Write-Ahead-Logging (WAL)

The Write Ahead Log (or WAL) is used to write and persist updates to disk on each tablet. The following table includes metrics which allow observing the performance of the WAL component:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `log_sync_latency` | microseconds | counter | The number of microseconds spent to fsync the WAL entries to disk. |
| `log_append_latency` | microseconds | counter | The number of microseconds spent on appending a batch of values to the WAL. |
| `log_group_commit_latency` | microseconds | counter | The number of microseconds spent on committing an entire group. |
| `log_bytes_logged`| bytes | counter | The number of bytes written to the WAL after the tablet starts. |
| `log_reader_bytes_read` | bytes | counter | The number of bytes read from WAL after the tablet start. |

These metrics are available per tablet and can be aggregated across the entire cluster using appropriate aggregations.

## YB-Master metrics

The [YB-Master](../../../architecture/concepts/yb-master/) hosts system metadata, records about tables in the system and locations of their tablets, users, roles, permissions, and so on. YB-Masters are also responsible for coordinating background operations such as schema changes, handling addition and removal of nodes from the cluster, automatic re-replication of data on permanent failures, and so on.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_master_MasterClient_GetTabletLocations` | microseconds | counter | The number of microseconds spent on fetching the replicas from the master servers. This metric includes the number of times the locations of the replicas from the master server. |
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | The time in microseconds to reads the PostgreSQL system tables (during DDL). This metric includes the count or number of writes. |
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | The time in microseconds to reads the PostgreSQL system tables (during DDL). This metric includes the count or number of writes. |
| `handler_latency_yb_master_MasterDdl_CreateTable` | microseconds | counter | The time in microseconds to create a table (during DDL). This metric includes the count of create table operations.|
| `handler_latency_yb_master_MasterDdl_DeleteTable` | microseconds | counter | The time in microseconds to delete a table (during DDL). This metric includes the count of delete table operations.|

A description of key metrics in this category is listed in the following table:

## Raft and distributed systems

### Raft operations, throughput, and latencies

YugabyteDB implements the RAFT consensus protocol, with minor modifications.
Replicas implement an RPC method called `UpdateConsensus` which allows a tablet leader to replicate a batch of log entries to the follower. Replicas also implement an RPC method called `RequestConsensusVote`, which candidates invoke to gather votes. `ChangeConfig` RPC method indicates the number of times a peer was added or removed from the consensus group. An increase in change configuration typically happens when YugabyteDB needs to move data around. This may happen due to a planned server addition or decommission or a server crash looping. A high number for the request consensus indicates that many replicas are looking for a new election because they have yet to receive a heartbeat from the leader. This could happen due to high CPU or a network partition condition.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_consensus_ConsensusService_UpdateConsensus` | microseconds | counter | The time in microseconds to replicate a batch of log entries from the leader to the follower. This metric includes the total count of the RPC method being invoked. |
| `handler_latency_yb_consensus_ConsensusService_RequestConsensusVotes` | microseconds | counter | The time in microseconds by candidates to gather votes. This metric includes the total count of the RPC method being invoked. |
| `handler_latency_yb_consensus_ConsensusService_ChangeConfig` | microseconds | counter | The time in microseconds by candidates to add or remove a peer from the Raft group. This metric includes the total count of the RPC method being invoked. |

The throughput (Ops/Sec) can be calculated and aggregated for nodes across the entire cluster using appropriate aggregations

### Clock skew

Clock skew is an important metric for performance and data consistency. It signals if the Hybrid Logical Clock (HLC) used by YugabyteDB is out of state or if your virtual machine was paused or migrated. If the skew is more than 500 milliseconds, it may impact the consistency guarantees of YugabyteDB. If there is unexplained, seemingly random latency in query responses and spikes in the clock skew metric, it could indicate that the virtual machine got migrated to another machine, or the hypervisor is oversubscribed.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `hybrid_clock_skew` | microseconds | gauge | The time in microseconds for clock drift and skew. |

### Remote Bootstraps

When a Raft peer fails, YugabyteDB executes an automatic remote bootstrap to create a new peer from the remaining ones. Bootstrapping can also result from planned user activity when adding or decommissioning nodes.

A description of key metrics in this category is listed in the following table:

| Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_consensus_ConsensusService_StartRemoteBootstrap` | microseconds | counter | The time in microseconds to remote bootstrap a new Raft peer. This metric includes the total count of remote bootstrap connections. |

This metric can be aggregated for nodes across the entire cluster using appropriate aggregations.
