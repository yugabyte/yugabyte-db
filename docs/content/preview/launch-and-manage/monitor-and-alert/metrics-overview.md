---
title: YugabyteDB metrics
headerTitle: YugabyteDB metrics
linkTitle: Metrics
description: Learn about YugabyteDB's database metrics, and how to select and use the metrics relevant to your situation
menu:
  preview:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 200
type: docs
---

YugabyteDB provides a very large number of metrics. This page describes many of the available metrics, their syntax and semantics, and how to view them for a specific use case.

## Overview

Each node in a YugabyteDB cluster exports metrics through JSON and Prometheus Exposition Format API endpoints. You can aggregate the metrics from each node to get a cluster-wide view. YugabyteDB Anywhere (YBA) and YugabyteDB Managed (YBM) aggregate a sub-set of relevant metrics from each node and provide an aggregated cluster view.

### Logical architecture components

| Component | Description | JSON API endpoints | Prometheus API endpoint |
| :-------- | :---------- | :----------------- | :---------------------------------------- |
| YSQL | YSQL statement and connection metrics. <br/> Starting point for all YSQL query-related heuristics. | | :13000/metrics | :13000/prometheus-metrics |
| YCQL | YCQL statement and connection metrics. <br/> Starting point for all YCQL query-related heuristics. | :12000/metrics | :12000/prometheus-metrics |
| Master | YugabyteDB master service, PostgreSQL, and storage-layer metrics. <br/> Debug-level metrics to troubleshoot system issues. | :7000/metrics | :7000/threadz | :7000/prometheus-metrics |
| TServer | YugabyteDB tablet server service, PostgreSQL, and storage-layer metrics. <br/> Debug-level metrics to troubleshoot system issues. | :9000/metrics | :9000/threadz | :9000/prometheus-metrics |
| Node Exporter | OS- and system-level metrics collected by Prometheus Node Exporter. <br/> Use to identify resource consumption trends, and correlate DocDB and system metrics to identify bottlenecks. | | :9300/prometheus-metrics |

### YSQL query-related metrics

|     | Description | API endpoint |
| :-: | :---------- | :----------- |
| pg_stat_statement | Per Node <br/> Historical view aggregated by normalizing queries. <br/> Equivalent view on YBA/YBM is _Slow Queries_. | :13000/statements |
| pg_stat_activity | Per Node <br/> Current normalized view of queries running on the node. <br/> Equivalent view on YBA/YBM is _Live Queries_. | :13000/rpcz |

### Metric categories

There are 4 major metric categories:

* [Server](#server-metrics) (logical architectural components) metrics per node
* [Table](#table-metrics) metrics per node
* [Tablet](#tablet-metrics) metrics per node
* [Cluster](#cluster-metrics) metrics per master

The majority of latency metrics expose count, sum, total, and quantiles (P99, P95, P50, mean, max), but a many of them may not be supported.

## Server metrics

These are identified by `"type": "server"` in the JSON API endpoint, and by an `export_type` of `tserver_export` in the Prometheus Exposition Format API endpoint.

### Logical service metrics

Service metric names are of the form `<metric_type>_<server_type>_<service_type>_<service_method>`.

**metric_type** (optional) can be one of:

* `handler_latency` is the latency as seen by the logical architecture block.
* `rpcs_in_queue` is the number of RPCs in queue for a service.
* `service_request_bytes` is the number of bytes sent by a service to other services in a request. This metric type is useful in a very limited number of cases.
* `service_response_bytes` is the number of bytes received by a service from other services in a request. This metric type is useful in a very limited number of cases.
* `proxy_request_bytes` is the number of request bytes sent by the proxy in a request to a service.  Anything that a client requires that the local tablet server cannot provide, is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers and awaiting a majority alias consensus to be reached. This metric type is useful in a very limited number of cases.
* `proxy_response_bytes` is the number of response bytes received by the proxy in from a service. Anything that a client requires that the local tablet server cannot provide, is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers and awaiting a majority alias consensus to be reached. This metric type is useful in a very limited number of cases.

**server_type** must be one of the following:

* `yb_tserver` - TServer metrics
* `yb_master` - Master metrics
* `yb_ycqlserver` - YCQL metrics
* `yb_ysqlserver` - YSQL metrics
* `yb_consesus` - RAFT consensus metrics
* `yb_cdc` - Change Data Capture metrics
* `yb_server` - ???

**service_type** is the logical service name for a given server type:

* yb_tserver service names:
  * PgClientService
  * TabletServerService
  * TabletServerAdminService
  * RemoteBootstrapService
  * TabletServerBackupService

* yb_master service names:
  * MasterAdmin
  * MasterClient
  * MasterBackupService
  * MasterBackup
  * MasterService
  * MasterReplication
  * MasterDdl
  * MasterCluster

* yb_ysqlserver service names:
  * SQLProcessor

* yb_cqlserver service names:
  * SQLProcessor

* yb_consensus service names:
  * ConsensusService

* yb_cdc service names:
  * CDCService

* yb_server service names:
  * GenericService

**service_method** (optional) identifies service methods, which are specific functions performed by the service.

### Other server metrics

Yugabyte exports other server metrics which do not conform to the preceding syntax and semantics. A list of these metrics is available from {{% support-general %}}.

## Table metrics

YugabyteDB exports table-level metrics on master and tablet servers. The comprehensive list of table metrics is in the [All table metrics](#all-table-metrics) table below.

Table metrics are identified by `"type": "server"` in the JSON API endpoint. In the Prometheus Exposition Format endpoint, they're identified by `export_types` of `tserver_export` and `master_export`.

Table metrics have the following attributes:

* `table_name` is the name of the table.
* `namespace_name` is the namespace of which the table is a part. Namespaces are logical constructs that can contain one or more tables.
* `table_id` is the YugabyteDB-generated unique table ID.

To identify the component to which a metric belongs:

* Metrics starting with `log_` are for WAL (write-ahead logging).
* Metrics starting with `rocksdb_` are for YugabyteDB's DocDB storage layer, which uses a highly modified RocksDB.

### All table metrics

The following table shows all `yb_tserver` and `yb_master` table metrics. The most commonly used table metrics are listed in **bold text**.

| Metric | Unit | Type |
| :----- | :--- | :--- |
| dns_resolve_latency_during_update_raft_config | microseconds | counter |
| **log_append_latency** | microseconds | counter |
| log_entry_batches_per_group | requests | counter |
| log_gc_duration | microseconds | counter |
| log_gc_running | operations | gauge |
| **log_group_commit_latency** | microseconds | counter |
| log_reader_read_batch_latency | microseconds | counter |
| log_roll_latency | microseconds | counter |
| **log_sync_latency** | microseconds | counter |
| ql_write_latency | microseconds | counter |
| ql_read_latency | microseconds | counter |
| rocksdb_bytes_per_multiget | bytes | counter |
| rocksdb_bytes_per_write | bytes | counter |
| **rocksdb_compaction_times_micros** | microseconds | counter |
| **rocksdb_db_get_micros** | microseconds | counter |
| rocksdb_db_multiget_micros | microseconds | counter |
| **rocksdb_db_seek_micros** | microseconds | counter |
| **rocksdb_db_write_micros** | microseconds | counter |
| **rocksdb_numfiles_in_singlecompaction** | files | counter |
| rocksdb_read_block_compaction_micros | microseconds | counter |

## Tablet metrics

YugabyteDB exports tablet-level metrics on master and tablet servers. The comprehensive list of tablet metrics is in the [All table metrics](#all-table-metrics) table below.

Tablet metrics are identified by `"type": "server"` in the JSON API endpoint. In the Prometheus Exposition Format endpoint, they're identified by `export_types` of `tserver_export` and `master_export`.

Tablet metrics have the following attributes:

* `table_name` is the name of the table of which this tablet is a part.
* `namespace_name` is the namespace of which the tablet's table is a part. Namespaces are logical constructs that can contain one or more tables.
* `table_id` is the YugabyteDB-generated unique ID for the table of which this tablet is a part.

To identify the component to which a metric belongs:

* Metrics starting with `log_` are for WAL (write-ahead logging).
* Metrics starting with `ql_` are for the table's query layer.
* Metrics starting with `rocksdb_` are for YugabyteDB's DocDB storage layer, which uses a highly modified RocksDB.

### All tablet metrics

The following table shows all `yb_tserver` and `yb_master` tablet metrics. The most commonly used tablet metrics are listed in **bold text**.

| Metric | Unit | Type |
| :----- | :--- | :--- |
| **all_operations_inflight** | operations | gauge |
| alter_schema_operations_inflight | operations | gauge |
| change_auto_flags_config_operations_inflight | operations | gauge |
| consistent_prefix_read_requests | requests | counter |
| empty_operations_inflight | operations | gauge |
| **follower_lag_ms** | milliseconds | gauge |
| **follower_memory_pressure_rejections** | rejections | counter |
| history_cutoff_operations_inflight | operations | gauge |
| is_raft_leader | indicator | gauge |
| **leader_memory_pressure_rejections** | rejections | counter |
| **log_bytes_logged** | bytes | counter |
| log_cache_disk_reads | reads | counter |
| **log_cache_num_ops** | operations | gauge |
| **log_cache_size** | bytes | gauge |
| **log_reader_bytes_read** | bytes | counter |
| log_reader_entries_read | entries | counter |
| log_wal_size | bytes | gauge |
| majority_done_ops | operations | gauge |
| **majority_sst_files_rejections** | rejections | counter |
| mem_tracker | bytes | gauge |
| mem_tracker_BlockBasedTable_IntentsDB | bytes | gauge |
| mem_tracker_BlockBasedTable_RegularDB | bytes | gauge |
| mem_tracker_IntentsDB | bytes | gauge |
| mem_tracker_IntentsDB_MemTable | bytes | gauge |
| mem_tracker_log_cache | bytes | gauge |
| mem_tracker_operation_tracker | bytes | gauge |
| mem_tracker_OperationsFromDisk | bytes | gauge |
| mem_tracker_RegularDB_MemTable | bytes | gauge |
| not_leader_rejections | rejections | counter |
| **operation_memory_pressure_rejections** | rejections | counter |
| pgsql_consistent_prefix_read_rows | rows | counter |
| raft_term | current consensus term | gauge |
| restart_read_requests | requests | counter |
| rocksdb_block_cache_add | blocks | counter |
| rocksdb_block_cache_add_failures | blocks | counter |
| rocksdb_block_cache_bytes_read | bytes | counter |
| rocksdb_block_cache_bytes_write | bytes | counter |
| rocksdb_block_cache_data_hit | blocks | counter |
| rocksdb_block_cache_data_miss | blocks | counter |
| rocksdb_block_cache_filter_hit | blocks | counter |
| rocksdb_block_cache_filter_miss | blocks | counter |
| **rocksdb_block_cache_hit** | blocks | counter |
| rocksdb_block_cache_index_hit | blocks | counter |
| rocksdb_block_cache_index_miss | blocks | counter |
| **rocksdb_block_cache_miss** | blocks | counter |
| rocksdb_block_cache_multi_touch_add | blocks | counter |
| rocksdb_block_cache_multi_touch_bytes_read | bytes | counter |
| rocksdb_block_cache_multi_touch_bytes_write | bytes | counter |
| rocksdb_block_cache_multi_touch_hit | blocks | counter |
| rocksdb_block_cache_single_touch_add | blocks | counter |
| rocksdb_block_cache_single_touch_bytes_read | bytes | counter |
| rocksdb_block_cache_single_touch_bytes_write | bytes | counter |
| rocksdb_block_cache_single_touch_hit | blocks | counter |
| rocksdb_block_cachecompressed_add | blocks | counter |
| rocksdb_block_cachecompressed_add_failures | blocks | counter |
| rocksdb_block_cachecompressed_hit | blocks | counter |
| rocksdb_block_cachecompressed_miss | blocks | counter |
| **rocksdb_bloom_filter_checked** | blocks | counter |
| rocksdb_bloom_filter_prefix_checked | blocks | counter |
| rocksdb_bloom_filter_prefix_useful | blocks | counter |
| **rocksdb_bloom_filter_useful** | blocks | counter |
| rocksdb_bytes_read | bytes | counter |
| rocksdb_bytes_written | bytes | counter |
| **rocksdb_compact_read_bytes** | bytes | counter |
| **rocksdb_compact_write_bytes** | bytes | counter |
| rocksdb_compaction_files_filtered | files | counter |
| rocksdb_compaction_files_not_filtered | files | counter |
| rocksdb_compaction_key_drop_new | keys | counter |
| rocksdb_compaction_key_drop_obsolete | keys | counter |
| rocksdb_compaction_key_drop_user | keys | counter |
| **rocksdb_current_version_num_sst_files** | files | gauge |
| **rocksdb_current_version_sst_files_size** | bytes | gauge |
| rocksdb_current_version_sst_files_uncompressed_size | bytes | gauge |
| rocksdb_db_iter_bytes_read | bytes | counter |
| **rocksdb_db_mutex_wait_micros** | microseconds | counter |
| rocksdb_filter_operation_time_nanos | nanoseconds | counter |
| **rocksdb_flush_write_bytes** | bytes | counter |
| rocksdb_getupdatessince_calls | calls | counter |
| rocksdb_l0_hit | keys | counter |
| rocksdb_l0_num_files_stall_micros | microseconds | counter |
| rocksdb_l0_slowdown_micros | microseconds | counter |
| rocksdb_l1_hit | keys | counter |
| rocksdb_l2andup_hit | keys | counter |
| rocksdb_memtable_compaction_micros | microseconds | counter |
| rocksdb_memtable_hit | keys | counter |
| rocksdb_memtable_miss | keys | counter |
| rocksdb_merge_operation_time_nanos | nanoseconds | counter |
| rocksdb_no_file_closes | files | counter |
| rocksdb_no_file_errors | files | counter |
| rocksdb_no_file_opens | files | counter |
| rocksdb_no_table_cache_iterators | iterators | counter |
| rocksdb_num_iterators | iterators | counter |
| rocksdb_number_block_not_compressed | blocks | counter |
| **rocksdb_number_db_next** | keys | counter |
| rocksdb_number_db_next_found | keys | counter |
| **rocksdb_number_db_prev** | keys | counter |
| rocksdb_number_db_prev_found | keys | counter |
| **rocksdb_number_db_seek** | keys | counter |
| rocksdb_number_db_seek_found | keys | counter |
| rocksdb_number_deletes_filtered | deletes | counter |
| rocksdb_number_direct_load_table_properties | properties | counter |
| rocksdb_number_keys_read | keys | counter |
| rocksdb_number_keys_updated | keys | counter |
| rocksdb_number_keys_written | keys | counter |
| rocksdb_number_merge_failures | failures | counter |
| rocksdb_number_multiget_bytes_read | bytes | counter |
| rocksdb_number_multiget_get | calls | counter |
| rocksdb_number_multiget_keys_read | keys | counter |
| rocksdb_number_reseeks_iteration | seeks | counter |
| rocksdb_number_superversion_acquires | nr | counter |
| rocksdb_number_superversion_cleanups | nr | counter |
| rocksdb_number_superversion_releases | nr | counter |
| rocksdb_rate_limit_delay_millis | milliseconds | counter |
| rocksdb_row_cache_hit | rows | counter |
| rocksdb_row_cache_miss | rows | counter |
| rocksdb_sequence_number | rows | counter |
| **rocksdb_stall_micros** | microseconds | counter |
| rocksdb_total_sst_files_size | bytes | gauge |
| rocksdb_wal_bytes | bytes | counter |
| rocksdb_wal_synced | syncs | counter |
| rocksdb_write_other | writes | counter |
| rocksdb_write_self | writes | counter |
| rocksdb_write_wal | writes | counter |
| rows_inserted | rows | counter |
| **snapshot_operations_inflight** | operations | gauge |
| split_operations_inflight | operations | gauge |
| transaction_not_found | transactions | counter |

## Cluster metrics

YugabyteDB exports cluster-level metrics. The comprehensive list of cluster metrics is in the [All cluster metrics](#all-cluster-metrics) table below.

Cluster metrics are identified by `"type": "server"` in the JSON API endpoint. In the Prometheus Exposition Format endpoint, they're identified by `export_types` of `master_export`.

### All cluster metrics

The following table shows all `yb_master` cluster metrics. The most commonly used cluster metrics are listed in **bold text**.

| Metric | Unit | Type |
| :----- | :--- | :--- |
| **is_load_balancing_enabled** | indicator | gauge |
| **num_tablet_servers_dead** | entries | gauge |
| **num_tablet_servers_live** | entries | gauge |
